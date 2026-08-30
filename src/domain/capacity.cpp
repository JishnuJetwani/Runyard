#include "runyard/domain/capacity.hpp"
#include <algorithm>
namespace runyard {
GpuCapacityTotals summarize(const ClusterGpuSnapshot &snapshot) {
  GpuCapacityTotals result;
  result.fresh = snapshot.fresh;
  result.pending = snapshot.pending;
  result.observed_at = snapshot.observed_at;
  result.age_seconds = snapshot.age_seconds;
  for (const auto &node : snapshot.nodes) {
    result.capacity += node.capacity;
    result.allocatable += node.allocatable;
    result.reserved += node.reserved;
    if (node.eligible)
      result.available += std::max<std::int64_t>(0, node.allocatable - node.reserved);
  }
  return result;
}
} // namespace runyard
