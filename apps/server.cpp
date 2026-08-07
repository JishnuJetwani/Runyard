#include "runyard/application/runs.hpp"
#include "runyard/grpc/services.hpp"
#include "runyard/http/api.hpp"
#include "runyard/postgres/leadership.hpp"
#include "runyard/postgres/store.hpp"
#include "runyard/storage/s3.hpp"
#include "runyard/support/config.hpp"
#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

int main(int argc, char **argv) {
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
    runyard::ConnectionPool pool(config.database);
    runyard::PostgresStore store(pool, config.timing);
    runyard::RunService runs(store);
    runyard::Executor executor;
    runyard::Leadership leadership(config.database);
    leadership.refresh();
    std::jthread monitor([&](std::stop_token stop) {
      while (!stop.stop_requested()) {
        if (leadership.refresh()) {
          try {
            store.recover();
          } catch (const std::exception &e) {
            spdlog::warn("recovery: {}", e.what());
          }
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
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
    runyard::AgentRpc agent_rpc(store, config, [&] { return leadership.ready(); });
    runyard::AttemptRpc attempt_rpc(store, artifacts, config, [&] { return leadership.ready(); });
    auto grpc_server = runyard::start_grpc(config, agent_rpc, attempt_rpc);
    auto &http = drogon::app();
    http.setThreadNum(2).setClientMaxBodySize(1024 * 1024);
    http.addListener(config.host, config.http_port, !config.certificate.empty(), config.certificate,
                     config.private_key);
    spdlog::info("coordinator HTTP port {} mode {}", config.http_port, config.mode);
    http.run();
    grpc_server->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(5));
    grpc_server->Wait();
    monitor.request_stop();
    monitor.join();
    executor.shutdown();
    return 0;
  } catch (const std::exception &e) {
    spdlog::error("{}", e.what());
    return 1;
  }
}
