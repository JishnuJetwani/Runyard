#include "runyard/runner/engine.hpp"
#include "runyard/domain/error.hpp"
#include "runyard/runner/environment.hpp"
#include "runyard/runner/process.hpp"
#include "runyard/runner/telemetry.hpp"
#include "runyard/serialization/json.hpp"
#include "runyard/support/crypto.hpp"
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace runyard {
namespace {
using Steady = std::chrono::steady_clock;
std::int64_t millis(Steady::time_point value) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(value.time_since_epoch()).count();
}
class LeaseGuard {
public:
  LeaseGuard(AttemptClient &client, Steady::time_point sent, int lease, int heartbeat)
      : until_(millis(sent + std::chrono::seconds(lease - 2))),
        thread_([&, heartbeat](std::stop_token stop) {
          std::mutex mutex;
          std::condition_variable_any wake;
          std::unique_lock lock(mutex);
          while (!stop.stop_requested()) {
            wake.wait_for(lock, stop, std::chrono::seconds(heartbeat), [] { return false; });
            if (stop.stop_requested())
              break;
            auto before = Steady::now();
            try {
              int seconds = client.heartbeat();
              until_ = millis(before + std::chrono::seconds(seconds - 2));
            } catch (const Error &e) {
              if (e.code() != ErrorCode::unavailable)
                revoked_ = true;
            }
          }
        }) {}
  bool revoked() const { return revoked_.load(); }
  bool near_expiry(int grace) const {
    return millis(Steady::now() + std::chrono::seconds(grace)) >= until_.load();
  }

private:
  std::atomic<std::int64_t> until_;
  std::atomic<bool> revoked_{false};
  std::jthread thread_;
};
} // namespace
int run_attempt(const RunnerConfig &config, const std::function<bool()> &stop_requested) {
  wire::Owner owner;
  owner.set_attempt_id(config.attempt_id);
  owner.set_generation(config.generation);
  owner.set_instance_id(random_id());
  AttemptClient client(make_channel(config.endpoint, config.ca_file, config.development), owner,
                       config.capability);
  auto sent = Steady::now();
  auto start = client.start();
  auto spec = decode_spec(Json::parse(start.specification_json()));
  LeaseGuard lease(client, sent, start.lease_seconds(), start.heartbeat_seconds());
  auto root = std::filesystem::absolute(config.directory) / config.attempt_id;
  std::filesystem::create_directories(root / "artifacts");
  root = std::filesystem::canonical(root);
  std::ofstream(root / "parameters.json") << encode(spec)["parameters"].dump(2);
  std::ofstream(root / "metrics.jsonl").close();
  auto environment =
      workload_environment(spec, runtime_environment(), root, config.run_id, config.generation);
  if (lease.near_expiry(start.termination_seconds()))
    throw Error(ErrorCode::stale, "initial lease has insufficient time to launch");
  SteadyClock clock;
  PosixProcess process(spec.command, environment, root.string(), clock);
  std::ofstream output(root / "stdout.log", std::ios::binary),
      error(root / "stderr.log", std::ios::binary);
  Reporter reporter(client);
  MetricReader metrics((root / "metrics.jsonl").string(), reporter);
  std::size_t log_bytes = 0;
  bool log_truncated = false;
  auto consume = [&](const std::string &kind, const std::string &text) {
    auto &stream = kind == "stdout" ? output : error;
    if (log_bytes + text.size() <= 100 * 1024 * 1024) {
      stream << text;
      log_bytes += text.size();
      reporter.log(kind, text);
    } else if (!log_truncated) {
      reporter.log("notice", "Log archive limit reached (100 MiB)\n");
      log_truncated = true;
    }
  };
  std::string reason;
  std::optional<int> status;
  auto execution_deadline = sent + std::chrono::seconds(spec.timeout_seconds);
  while (!status) {
    if (reason.empty()) {
      if (stop_requested())
        reason = "STOP_REQUESTED";
      else if (lease.revoked() || lease.near_expiry(start.termination_seconds()))
        reason = "LEASE_LOST";
      else if (Steady::now() >= execution_deadline)
        reason = "TIMEOUT";
      if (!reason.empty())
        process.stop(std::chrono::seconds(start.termination_seconds()));
    }
    process.drain(consume);
    metrics.poll();
    status = process.poll();
    if (!status)
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  process.drain(consume);
  output.close();
  error.close();
  if (lease.revoked() || lease.near_expiry(0))
    return 1;
  client.begin_finalization();
  metrics.poll();
  auto sequence = reporter.flush(Steady::now() + std::chrono::seconds(30));
  // A child can replace the output root or archive names while it runs.
  if (std::filesystem::canonical(root) != root ||
      !std::filesystem::is_directory(std::filesystem::symlink_status(root / "artifacts")))
    throw Error(ErrorCode::invalid, "artifact root must remain a directory, not a symlink");
  auto upload_file = [&](const std::filesystem::path &path, const std::string &relative) {
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path)))
      throw Error(ErrorCode::invalid, "artifact must be a regular file, not a symlink");
    client.upload(path.string(), relative);
  };
  for (const auto &entry : std::filesystem::recursive_directory_iterator(root / "artifacts")) {
    if (entry.is_symlink())
      throw Error(ErrorCode::invalid, "artifact symlinks are not supported");
    if (!entry.is_regular_file())
      continue;
    auto relative = std::filesystem::relative(entry.path(), root / "artifacts").generic_string();
    if (relative == "_runyard" || relative.starts_with("_runyard/"))
      throw Error(ErrorCode::invalid, "_runyard artifact namespace is reserved");
    upload_file(entry.path(), relative);
  }
  upload_file(root / "stdout.log", "_runyard/stdout.log");
  upload_file(root / "stderr.log", "_runyard/stderr.log");
  client.complete(!reason.empty() && *status == 0 ? 143 : *status, reason, sequence);
  return *status;
}
} // namespace runyard
