#include "runyard/application/kubernetes.hpp"
#include "runyard/application/runs.hpp"
#include "runyard/execution/kubernetes.hpp"
#include "runyard/grpc/services.hpp"
#include "runyard/http/api.hpp"
#include "runyard/postgres/leadership.hpp"
#include "runyard/postgres/store.hpp"
#include "runyard/storage/s3.hpp"
#include "runyard/support/config.hpp"
#include <CLI/CLI.hpp>
#include <condition_variable>
#include <spdlog/spdlog.h>
#include <sys/resource.h>

namespace {
void pause(std::stop_token stop, std::chrono::milliseconds duration) {
  std::mutex mutex;
  std::condition_variable_any wake;
  std::unique_lock lock(mutex);
  wake.wait_for(lock, stop, duration, [] { return false; });
}
} // namespace

int main(int argc, char **argv) {
  runyard::structured_logging();
  CLI::App app{"Runyard coordinator"};
  app.require_subcommand();
  auto *migration = app.add_subcommand("migrate", "Apply numbered database migrations");
  std::string directory = "migrations";
  migration->add_option("--directory", directory);
  app.add_subcommand("serve", "Start the coordinator");
  CLI11_PARSE(app, argc, argv);
  try {
    if (*migration) {
      runyard::ConnectionPool pool(runyard::env("RUNYARD_DATABASE_URL"), 1);
      runyard::migrate(pool, directory);
      return 0;
    }
    auto config = runyard::ServerConfig::load();
    runyard::Metrics metrics;
    metrics.update({{"kubernetes_mode", config.mode == "kubernetes" ? 1.0 : 0.0}});
    runyard::ConnectionPool pool(config.database);
    runyard::PostgresStore store(pool, config.timing);
    runyard::RunService runs(store);
    runyard::Executor executor;
    runyard::Leadership leadership(config.database);
    leadership.refresh();
    std::unique_ptr<runyard::KubernetesBackend> kubernetes_backend;
    std::unique_ptr<runyard::KubernetesController> kubernetes_controller;
    if (config.mode == "kubernetes") {
      runyard::KubernetesConfig k;
      k.api = runyard::env("RUNYARD_KUBERNETES_API", k.api);
      k.name_space = runyard::env("RUNYARD_KUBERNETES_NAMESPACE", k.name_space);
      k.coordinator = runyard::env("RUNYARD_RUNNER_COORDINATOR", k.coordinator);
      k.development = config.development;
      k.runner_ca_configmap = runyard::env("RUNYARD_RUNNER_CA_CONFIGMAP");
      kubernetes_backend = std::make_unique<runyard::KubernetesBackend>(k);
      kubernetes_controller = std::make_unique<runyard::KubernetesController>(
          store, *kubernetes_backend, config.signing_key,
          runyard::env_int("RUNYARD_MAX_ACTIVE_JOBS", 16));
    }
    std::jthread monitor([&](std::stop_token stop) {
      while (!stop.stop_requested()) {
        metrics.update({{"ready", leadership.refresh() ? 1.0 : 0.0}});
        if (leadership.ready()) {
          try {
            store.recover();
            auto snapshot = store.statistics();
            auto pool_stats = pool.statistics();
            snapshot.insert(pool_stats.begin(), pool_stats.end());
            snapshot["ready"] = leadership.ready() ? 1 : 0;
            struct rusage usage{};
            getrusage(RUSAGE_SELF, &usage);
            snapshot["process_cpu_seconds"] = usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6 +
                                              usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6;
#ifdef __APPLE__
            snapshot["process_max_rss_bytes"] = usage.ru_maxrss;
#else
            snapshot["process_max_rss_bytes"] = usage.ru_maxrss * 1024.0;
#endif
            metrics.update(snapshot);

          } catch (const std::exception &e) {
            spdlog::warn("recovery: {}", e.what());
          }
        }
        pause(stop, std::chrono::milliseconds(config.timing.recovery_scan_millis));
      }
    });
    std::jthread dispatch([&](std::stop_token stop) {
      while (!stop.stop_requested()) {
        if (leadership.ready() && kubernetes_controller) {
          try {
            kubernetes_controller->tick();
          } catch (const std::exception &e) {
            spdlog::warn("Kubernetes reconciliation: {}", e.what());
          }
        }
        pause(stop, std::chrono::milliseconds(config.timing.recovery_scan_millis));
      }
    });
    std::unique_ptr<runyard::BlobStore> blobs;
    auto storage = runyard::env("RUNYARD_STORAGE", "filesystem");
    if (storage == "s3")
      blobs = std::make_unique<runyard::S3Store>(runyard::S3Config{
          runyard::env("RUNYARD_S3_BUCKET"), runyard::env("AWS_REGION", "us-east-1"),
          runyard::env("RUNYARD_S3_ENDPOINT"), config.development});
    else if (storage == "filesystem")
      blobs = std::make_unique<runyard::FilesystemStore>(config.artifacts + "/objects");
    else
      throw std::runtime_error("storage must be filesystem or s3");
    runyard::ArtifactService artifacts(store, *blobs, config.artifacts);
    runyard::Api api(runs, artifacts, executor, config.owner_token,
                     [&] { return leadership.ready(); });
    api.mount();
    drogon::app().registerHandler(
        "/metrics",
        [&metrics](const drogon::HttpRequestPtr &, runyard::HttpCallback &&callback) {
          auto response = drogon::HttpResponse::newHttpResponse();
          response->setContentTypeString("text/plain; version=0.0.4");
          response->setBody(metrics.render());
          callback(response);
        },
        {drogon::Get});
    runyard::AgentRpc agent_rpc(store, config, [&] { return leadership.ready(); }, &metrics);
    runyard::AttemptRpc attempt_rpc(
        store, artifacts, config, [&] { return leadership.ready(); }, &metrics);
    auto grpc_server = runyard::start_grpc(config, agent_rpc, attempt_rpc);
    auto &http = drogon::app();
    http.setThreadNum(2).setClientMaxBodySize(1024 * 1024);
    http.addListener(config.host, config.http_port, !config.certificate.empty(), config.certificate,
                     config.private_key);
    spdlog::info("coordinator HTTP port {} mode {}", config.http_port, config.mode);
    http.run();
    grpc_server->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(5));
    grpc_server->Wait();
    dispatch.request_stop();
    dispatch.join();
    monitor.request_stop();
    monitor.join();
    executor.shutdown();
    return 0;
  } catch (const std::exception &e) {
    spdlog::error("{}", e.what());
    return 1;
  }
}
