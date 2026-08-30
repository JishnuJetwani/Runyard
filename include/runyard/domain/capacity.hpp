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

namespace runyard {
struct GpuCapacityTotals {
  std::int64_t capacity{};
  std::int64_t allocatable{};
  std::int64_t reserved{};
  std::int64_t available{};
  std::int64_t pending{};
  bool fresh{};
  std::string observed_at;
  std::optional<std::int64_t> age_seconds;
};
struct CapacityPage {
  std::string backend;
  GpuCapacityTotals gpu;
  std::vector<Worker> workers;
  std::vector<GpuNodeCapacity> nodes;
  std::string next_cursor;
};
GpuCapacityTotals summarize(const ClusterGpuSnapshot &);
} // namespace runyard
