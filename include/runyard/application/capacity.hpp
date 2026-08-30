#pragma once
#include "runyard/application/repository.hpp"
#include <functional>
namespace runyard {
class CapacityService {
public:
  explicit CapacityService(Repository &repository, std::function<ClusterGpuSnapshot()> cluster = {})
      : repository_(repository), cluster_(std::move(cluster)) {}
  CapacityPage get(int limit, const std::string &after);

private:
  Repository &repository_;
  std::function<ClusterGpuSnapshot()> cluster_;
};
} // namespace runyard
