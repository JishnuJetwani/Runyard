#pragma once
#include "runyard/runner/client.hpp"
#include <functional>

namespace runyard {
struct RunnerConfig {
  std::string attempt_id;
  std::string run_id;
  int generation;
  std::string endpoint;
  std::string capability;
  std::string ca_file;
  std::string directory;
  bool development;
};
int run_attempt(const RunnerConfig &config, const std::function<bool()> &stop_requested);
} // namespace runyard
