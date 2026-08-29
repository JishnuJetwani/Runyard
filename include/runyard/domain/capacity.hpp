#pragma once
#include "runyard/domain/model.hpp"

namespace runyard {
struct GpuNodeCapacity {
  std::string name;
  std::int64_t capacity{};
  std::int64_t allocatable{};
  std::int64_t reserved{};
  bool ready{};
  bool schedulable{};
  bool eligible{};
};
struct ClusterGpuSnapshot {
  std::vector<GpuNodeCapacity> nodes;
  std::int64_t pending{};
  std::string observed_at;
  std::optional<std::int64_t> age_seconds;
  bool fresh{};
};
} // namespace runyard
