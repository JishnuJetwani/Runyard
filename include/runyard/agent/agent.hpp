#pragma once
#include "runyard/execution/backend.hpp"
#include "runyard/grpc/services.hpp"
#include <functional>

namespace runyard {
struct AgentConfig {
  std::string id;
  std::string endpoint;
  std::string token;
  std::string ca_file;
  Resources capacity;
  bool development;
};
void run_agent(const AgentConfig &, ExecutionBackend &,
               const std::function<bool()> &stop_requested);
} // namespace runyard
