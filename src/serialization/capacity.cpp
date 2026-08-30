#include "runyard/serialization/json.hpp"
#include <algorithm>
namespace runyard {
Json encode(const GpuDevice &d) {
  return {{"uuid", d.uuid},
          {"name", d.name},
          {"memory_mib", d.memory_mib},
          {"eligible", d.eligible},
          {"reason", d.reason}};
}
Json encode(const GpuAllocation &a) {
  return {{"device", encode(a.device)},
          {"allocated_at", a.allocated_at},
          {"released_at", a.released_at}};
}
Json encode(const CapacityPage &page) {
  const auto &g = page.gpu;
  Json gpu = {{"capacity", g.capacity},
              {"allocatable", g.allocatable},
              {"reserved", g.reserved},
              {"available_estimate", g.fresh ? Json(g.available) : Json(nullptr)},
              {"pending", g.pending},
              {"fresh", g.fresh},
              {"observed_at", g.observed_at},
              {"age_seconds", g.age_seconds ? Json(*g.age_seconds) : Json(nullptr)}};
  Json nodes = Json::array();
  for (const auto &node : page.nodes)
    nodes.push_back(
        {{"name", node.name},
         {"capacity", node.capacity},
         {"allocatable", node.allocatable},
         {"reserved", node.reserved},
         {"ready", node.ready},
         {"schedulable", node.schedulable},
         {"eligible", node.eligible},
         {"available_estimate",
          g.fresh ? Json(node.eligible ? std::max<std::int64_t>(0, node.allocatable - node.reserved)
                                       : 0)
                  : Json(nullptr)}});
  return {{"backend", page.backend},
          {"gpu", gpu},
          {"workers", encode_list(page.workers)},
          {"nodes", nodes},
          {"next_cursor", page.next_cursor}};
}
} // namespace runyard
