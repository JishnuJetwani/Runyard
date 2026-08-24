#include "runyard/runner/environment.hpp"
#include "runyard/domain/error.hpp"
#include <array>
#include <cstdlib>
#include <sstream>
#include <unistd.h>

namespace runyard {
namespace {
constexpr std::array allowed{"PATH",
                             "LD_LIBRARY_PATH",
                             "PYTHONPATH",
                             "VIRTUAL_ENV",
                             "CUDA_HOME",
                             "CUDA_PATH",
                             "CUDA_VISIBLE_DEVICES",
                             "NVIDIA_VISIBLE_DEVICES",
                             "NVIDIA_DRIVER_CAPABILITIES"};
constexpr auto default_path = "/usr/local/bin:/usr/bin:/bin";
} // namespace
Environment runtime_environment() {
  Environment result;
  for (const auto *name : allowed)
    if (auto value = std::getenv(name))
      result[name] = value;
  return result;
}
Environment workload_environment(const RunSpec &spec, const Environment &inherited,
                                 const std::filesystem::path &root, const std::string &run_id,
                                 int generation) {
  Environment result{{"PATH", default_path}, {"LANG", "C.UTF-8"}};
  for (const auto *name : allowed)
    if (auto it = inherited.find(name); it != inherited.end())
      result[name] = it->second;
  for (const auto &[name, value] : spec.environment)
    result[name] = value;
  // Historical specs may contain NVIDIA variables. Runtime allocation still wins.
  for (const auto *name : {"NVIDIA_VISIBLE_DEVICES", "NVIDIA_DRIVER_CAPABILITIES"}) {
    result.erase(name);
    if (auto it = inherited.find(name); it != inherited.end())
      result[name] = it->second;
  }
  if (spec.resources.gpu_count == 0) {
    result["NVIDIA_VISIBLE_DEVICES"] = "void";
    result.erase("NVIDIA_DRIVER_CAPABILITIES");
  }
  result["HOME"] = root.string();
  result["RUNYARD_PARAMETERS_PATH"] = (root / "parameters.json").string();
  result["RUNYARD_METRICS_PATH"] = (root / "metrics.jsonl").string();
  result["RUNYARD_OUTPUT_DIR"] = (root / "artifacts").string();
  result["RUNYARD_RUN_ID"] = run_id;
  result["RUNYARD_ATTEMPT_NUMBER"] = std::to_string(generation);
  return result;
}
std::string resolve_executable(const std::string &command, const Environment &environment,
                               const std::filesystem::path &directory) {
  auto absolute = [&](const std::filesystem::path &path) {
    return path.is_absolute() ? path : std::filesystem::absolute(directory / path);
  };
  if (command.find('/') != std::string::npos)
    return absolute(command).string();
  auto it = environment.find("PATH");
  std::istringstream paths(it == environment.end() ? default_path : it->second);
  std::string part;
  // Empty PATH entries refer to the child's working directory, not the coordinator's.
  do {
    std::getline(paths, part, ':');
    auto candidate = absolute(std::filesystem::path(part) / command);
    std::error_code error;
    if (std::filesystem::is_regular_file(candidate, error) && access(candidate.c_str(), X_OK) == 0)
      return candidate.string();
  } while (paths.good());
  throw Error(ErrorCode::invalid, "command not executable on workload PATH: " + command);
}
} // namespace runyard
