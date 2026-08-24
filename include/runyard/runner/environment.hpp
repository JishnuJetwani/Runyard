#pragma once
#include "runyard/domain/model.hpp"
#include <filesystem>

namespace runyard {
using Environment = std::map<std::string, std::string>;
Environment runtime_environment();
Environment workload_environment(const RunSpec &, const Environment &inherited,
                                 const std::filesystem::path &root, const std::string &run_id,
                                 int generation);
std::string resolve_executable(const std::string &command, const Environment &,
                               const std::filesystem::path &directory);
} // namespace runyard
