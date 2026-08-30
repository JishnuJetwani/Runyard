#include "runyard/observability/metrics.hpp"
#include <limits>
#include <prometheus/text_serializer.h>

namespace runyard {
Metrics::Metrics()
    : rpc_duration_(prometheus::BuildHistogram()
                        .Name("runyard_rpc_duration_seconds")
                        .Help("Completed RPC duration")
                        .Register(registry_)),
      rpc_requests_(prometheus::BuildCounter()
                        .Name("runyard_rpc_requests_total")
                        .Help("RPC requests by method and gRPC status code")
                        .Register(registry_)) {}
void Metrics::update(const std::map<std::string, double> &values) {
  std::lock_guard lock(mutex_);
  for (const auto &[name, value] : values) {
    auto it = gauges_.find(name);
    if (it == gauges_.end())
      it = gauges_
               .emplace(name, &prometheus::BuildGauge()
                                   .Name("runyard_" + name)
                                   .Help("Coordinator snapshot: " + name)
                                   .Register(registry_)
                                   .Add({}))
               .first;
    it->second->Set(value);
  }
}
void Metrics::gpus(const GpuCapacityTotals &g) {
  const auto unknown = std::numeric_limits<double>::quiet_NaN();
  update({{"gpu_capacity", static_cast<double>(g.capacity)},
          {"gpu_allocatable", static_cast<double>(g.allocatable)},
          {"gpu_reserved", static_cast<double>(g.reserved)},
          {"gpu_available_estimate", g.fresh ? static_cast<double>(g.available) : unknown},
          {"gpu_pending", static_cast<double>(g.pending)},
          {"gpu_inventory_fresh", g.fresh ? 1 : 0},
          {"gpu_inventory_age_seconds",
           g.age_seconds ? static_cast<double>(*g.age_seconds) : unknown}});
}
void Metrics::rpc(const std::string &method, int status, double seconds) {
  std::lock_guard lock(mutex_);
  rpc_duration_
      .Add({{"method", method}},
           prometheus::Histogram::BucketBoundaries{.001, .005, .01, .05, .1, .5, 1, 5, 30, 300})
      .Observe(seconds);
  rpc_requests_.Add({{"method", method}, {"status", std::to_string(status)}}).Increment();
}
std::string Metrics::render() const {
  std::lock_guard lock(mutex_);
  return prometheus::TextSerializer{}.Serialize(registry_.Collect());
}
} // namespace runyard
