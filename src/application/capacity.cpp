#include "runyard/application/capacity.hpp"
#include "runyard/domain/error.hpp"
namespace runyard {
CapacityPage CapacityService::get(int limit, const std::string &after) {
  if (limit < 1 || limit > 200 || after.size() > 253)
    throw Error(ErrorCode::invalid, "invalid capacity pagination");
  CapacityPage result;
  if (cluster_) {
    auto snapshot = cluster_();
    result.backend = "kubernetes";
    result.gpu = summarize(snapshot);
    for (const auto &node : snapshot.nodes) {
      if (node.name <= after)
        continue;
      if (result.nodes.size() == static_cast<std::size_t>(limit))
        break;
      result.nodes.push_back(node);
    }
    if (result.nodes.size() == static_cast<std::size_t>(limit))
      result.next_cursor = result.nodes.back().name;
  } else {
    result.backend = "docker";
    result.gpu = repository_.gpu_capacity();
    result.workers = repository_.workers(limit, after);
    if (result.workers.size() == static_cast<std::size_t>(limit))
      result.next_cursor = result.workers.back().id;
  }
  return result;
}
} // namespace runyard
