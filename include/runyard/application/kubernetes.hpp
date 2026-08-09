#pragma once
#include "runyard/application/repository.hpp"
#include "runyard/execution/backend.hpp"
namespace runyard {
class KubernetesController {
public:
  KubernetesController(Repository &repository, ExecutionBackend &backend, std::string signing_key,
                       int max_active)
      : repository_(repository), backend_(backend), signing_key_(std::move(signing_key)),
        max_active_(max_active) {}
  void tick();

private:
  void launch(const Assignment &);
  Repository &repository_;
  ExecutionBackend &backend_;
  std::string signing_key_;
  int max_active_;
};
} // namespace runyard
