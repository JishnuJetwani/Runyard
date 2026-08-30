#pragma once
#include "runyard/domain/capacity.hpp"
#include <map>
#include <mutex>
#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/registry.h>
#include <string>

namespace runyard {
class Metrics {
public:
  Metrics();
  void update(const std::map<std::string, double> &values);
  void gpus(const GpuCapacityTotals &);
  void rpc(const std::string &method, int status, double seconds);
  std::string render() const;

private:
  prometheus::Registry registry_;
  prometheus::Family<prometheus::Histogram> &rpc_duration_;
  prometheus::Family<prometheus::Counter> &rpc_requests_;
  std::map<std::string, prometheus::Gauge *> gauges_;
  mutable std::mutex mutex_;
};
void structured_logging();
} // namespace runyard
