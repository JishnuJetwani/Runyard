#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace runyard {
using Scalar = std::variant<std::nullptr_t, bool, std::int64_t, double, std::string>;
using Parameters = std::map<std::string, Scalar>;

enum class RunStatus {
  queued,
  starting,
  running,
  finalizing,
  retry_wait,
  succeeded,
  failed,
  cancelled
};
enum class Failure { infrastructure, exit_error, timeout, cancelled };
std::string to_string(RunStatus status);
RunStatus parse_status(const std::string &value);
bool terminal(RunStatus status);
bool transition_allowed(RunStatus from, RunStatus to);

struct Resources {
  int cpu_millis{1000};
  int memory_mib{512};
  bool fits(const Resources &available) const;
};

struct RetryPolicy {
  int max_attempts{3};
  bool retry_exit{false};
  bool retry_timeout{false};
};

struct RunSpec {
  int version{1};
  std::string name;
  std::string image;
  std::vector<std::string> command;
  Parameters parameters;
  std::map<std::string, std::string> environment;
  std::map<std::string, std::string> labels;
  Resources resources;
  RetryPolicy retry;
  int timeout_seconds{1800};
  int priority{0};
  std::string source_repository;
  std::string source_revision;
};

struct Run {
  std::string id;
  RunSpec spec;
  RunStatus status{RunStatus::queued};
  int generation{0};
  std::string active_attempt;
  std::string sweep_id;
  std::string parent_run_id;
  std::string created_at;
  std::string updated_at;
};

struct Attempt {
  std::string id;
  std::string run_id;
  int generation{};
  std::string worker_id;
  std::string instance_id;
  std::string status;
  std::string reason;
  std::optional<int> exit_code;
  std::string runtime_id;
  std::string cleanup_status;
  std::string created_at;
  std::string started_at;
  std::string finished_at;
  std::int64_t acknowledged_sequence{};
};

struct Assignment {
  Attempt attempt;
  RunSpec spec;
};

struct Worker {
  std::string id;
  std::string session;
  Resources capacity;
  Resources reserved{0, 0};
  bool drained{};
  bool available{};
  std::string heartbeat_at;
};

struct Telemetry {
  std::int64_t sequence{};
  std::string kind;
  std::string text;
  std::string name;
  std::int64_t step{};
  double value{};
  std::int64_t timestamp_ms{};
};

struct Event {
  std::int64_t sequence{};
  std::string kind;
  std::string detail;
  std::string created_at;
};

struct Artifact {
  std::string id;
  std::string attempt_id;
  std::string path;
  std::string storage_key;
  std::string sha256;
  std::uint64_t size{};
};

struct Timing {
  int heartbeat_seconds{5};
  int lease_seconds{30};
  int worker_seconds{15};
  int launch_seconds{300};
  int retry_base_seconds{5};
  int termination_seconds{10};
  int finalization_seconds{300};
};

class Clock {
public:
  virtual ~Clock() = default;
  virtual std::chrono::steady_clock::time_point now() const = 0;
};
class SteadyClock final : public Clock {
public:
  std::chrono::steady_clock::time_point now() const override {
    return std::chrono::steady_clock::now();
  }
};

void validate(const RunSpec &spec);
void validate(const Telemetry &point);
void validate_relative_path(const std::string &path);
bool should_retry(const RetryPolicy &policy, int completed_attempts, Failure reason);
int retry_delay(int generation, int base_seconds = 5);
std::vector<RunSpec> expand_sweep(const RunSpec &base,
                                  const std::map<std::string, std::vector<Scalar>> &grid);
} // namespace runyard
