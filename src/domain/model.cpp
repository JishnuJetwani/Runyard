#include "runyard/domain/model.hpp"
#include "runyard/domain/error.hpp"

#include <algorithm>
#include <cmath>
#include <regex>

namespace runyard {
namespace {
const std::vector<std::string> statuses{"QUEUED",     "STARTING",  "RUNNING", "FINALIZING",
                                        "RETRY_WAIT", "SUCCEEDED", "FAILED",  "CANCELLED"};
void require(bool condition, const std::string &message) {
  if (!condition)
    throw Error(ErrorCode::invalid, message);
}
bool safe_string(const std::string &value) { return value.find('\0') == std::string::npos; }
} // namespace

std::string to_string(RunStatus status) { return statuses.at(static_cast<std::size_t>(status)); }
RunStatus parse_status(const std::string &value) {
  auto it = std::find(statuses.begin(), statuses.end(), value);
  if (it == statuses.end())
    throw Error(ErrorCode::invalid, "unknown run status: " + value);
  return static_cast<RunStatus>(std::distance(statuses.begin(), it));
}
bool terminal(RunStatus status) {
  return status == RunStatus::succeeded || status == RunStatus::failed ||
         status == RunStatus::cancelled;
}
bool transition_allowed(RunStatus from, RunStatus to) {
  if (terminal(from))
    return false;
  if (to == RunStatus::cancelled)
    return true;
  switch (from) {
  case RunStatus::queued:
    return to == RunStatus::starting;
  case RunStatus::starting:
    return to == RunStatus::running || to == RunStatus::retry_wait || to == RunStatus::failed;
  case RunStatus::running:
    return to == RunStatus::finalizing || to == RunStatus::retry_wait || to == RunStatus::failed;
  case RunStatus::finalizing:
    return to == RunStatus::succeeded || to == RunStatus::retry_wait || to == RunStatus::failed;
  case RunStatus::retry_wait:
    return to == RunStatus::queued;
  default:
    return false;
  }
}
bool Resources::fits(const Resources &available) const {
  return cpu_millis <= available.cpu_millis && memory_mib <= available.memory_mib;
}

void validate(const RunSpec &spec) {
  static const std::regex image(R"(^[a-zA-Z0-9][a-zA-Z0-9._:/-]*@sha256:[a-f0-9]{64}$)");
  static const std::regex env_name(R"(^[A-Za-z_][A-Za-z0-9_]*$)");
  require(spec.version == 1, "specification version must be 1");
  require(!spec.name.empty() && spec.name.size() <= 200 && safe_string(spec.name),
          "invalid run name");
  require(spec.image.size() <= 1024 && std::regex_match(spec.image, image),
          "image must be pinned with @sha256:<64 lowercase hex digits>");
  require(!spec.command.empty() && spec.command.size() <= 256 && !spec.command[0].empty(),
          "command must be a nonempty argv array");
  for (const auto &arg : spec.command)
    require(arg.size() <= 16384 && safe_string(arg), "invalid command argument");
  require(spec.resources.cpu_millis > 0 && spec.resources.cpu_millis <= 1000000,
          "cpu_millis out of range");
  require(spec.resources.memory_mib >= 32 && spec.resources.memory_mib <= 1048576,
          "memory_mib out of range");
  require(spec.timeout_seconds > 0 && spec.timeout_seconds <= 604800,
          "timeout_seconds must be between 1 and 604800");
  require(spec.priority >= 0 && spec.priority <= 9, "priority must be between 0 and 9");
  require(spec.retry.max_attempts >= 1 && spec.retry.max_attempts <= 10,
          "max_attempts must be between 1 and 10");
  require(spec.parameters.size() <= 100 && spec.labels.size() <= 50 &&
              spec.environment.size() <= 100,
          "too many specification fields");
  require(spec.source_repository.size() <= 2048 && safe_string(spec.source_repository) &&
              spec.source_revision.size() <= 200 && safe_string(spec.source_revision),
          "invalid source metadata");
  for (const auto &[key, value] : spec.labels)
    require(!key.empty() && key.size() <= 200 && value.size() <= 1024 && safe_string(key) &&
                safe_string(value),
            "invalid label");
  for (const auto &[key, value] : spec.environment) {
    require(std::regex_match(key, env_name) && !key.starts_with("RUNYARD_"),
            "invalid or reserved environment name");
    require(value.size() <= 16384 && safe_string(value), "invalid environment value");
  }
  for (const auto &[key, value] : spec.parameters) {
    require(!key.empty() && key.size() <= 200 && safe_string(key), "invalid parameter name");
    if (auto number = std::get_if<double>(&value))
      require(std::isfinite(*number), "parameter must be finite");
    if (auto text = std::get_if<std::string>(&value))
      require(text->size() <= 16384 && safe_string(*text), "invalid parameter value");
  }
}

void validate(const Telemetry &point) {
  require(point.sequence > 0, "telemetry sequence must be positive");
  require(point.kind == "stdout" || point.kind == "stderr" || point.kind == "metric" ||
              point.kind == "notice",
          "unknown telemetry kind");
  require(point.text.size() <= 65536 && safe_string(point.text), "log record exceeds 64 KiB");
  if (point.kind == "metric") {
    require(!point.name.empty() && point.name.size() <= 200 && safe_string(point.name),
            "invalid metric name");
    require(std::isfinite(point.value) && point.step >= 0, "invalid metric value or step");
  }
}

void validate_relative_path(const std::string &path) {
  require(!path.empty() && path.size() <= 1024 && path.front() != '/' && safe_string(path),
          "invalid artifact path");
  require(path.find('\\') == std::string::npos, "artifact paths use forward slashes");
  std::size_t begin = 0;
  while (begin <= path.size()) {
    auto end = path.find('/', begin);
    auto part = path.substr(begin, end == std::string::npos ? end : end - begin);
    require(!part.empty() && part != "." && part != "..",
            "artifact path contains an unsafe component");
    if (end == std::string::npos)
      break;
    begin = end + 1;
  }
}

bool should_retry(const RetryPolicy &policy, int completed_attempts, Failure reason) {
  if (completed_attempts >= policy.max_attempts || reason == Failure::cancelled)
    return false;
  return reason == Failure::infrastructure ||
         (reason == Failure::exit_error && policy.retry_exit) ||
         (reason == Failure::timeout && policy.retry_timeout);
}
int retry_delay(int generation, int base_seconds) {
  return std::min(60, std::clamp(base_seconds, 1, 60) * (1 << std::clamp(generation - 1, 0, 6)));
}

std::vector<RunSpec> expand_sweep(const RunSpec &base,
                                  const std::map<std::string, std::vector<Scalar>> &grid) {
  validate(base);
  std::size_t count = 1;
  for (const auto &[key, values] : grid) {
    require(!key.empty() && !values.empty(), "sweep dimensions cannot be empty");
    require(values.size() <= 1000 / count, "sweep exceeds 1000 runs");
    count *= values.size();
  }
  std::size_t bytes = 4096 + base.name.size() + base.image.size();
  for (const auto &arg : base.command)
    bytes += arg.size();
  for (const auto &[key, value] : base.environment)
    bytes += key.size() + value.size();
  for (const auto &[key, value] : base.parameters) {
    bytes += key.size() + 32;
    if (auto text = std::get_if<std::string>(&value))
      bytes += text->size();
  }
  for (const auto &[key, values] : grid) {
    std::size_t largest = 0;
    for (const auto &value : values)
      if (auto text = std::get_if<std::string>(&value))
        largest = std::max(largest, text->size());
    bytes += key.size() + largest + 32;
  }
  require(bytes <= 16 * 1024 * 1024 / count, "expanded sweep exceeds 16 MiB");
  std::vector<RunSpec> result{base};
  for (const auto &[key, values] : grid) {
    std::vector<RunSpec> expanded;
    for (const auto &spec : result) {
      for (const auto &value : values) {
        auto item = spec;
        item.parameters[key] = value;
        validate(item);
        expanded.push_back(std::move(item));
      }
    }
    result = std::move(expanded);
  }
  return result;
}
} // namespace runyard
