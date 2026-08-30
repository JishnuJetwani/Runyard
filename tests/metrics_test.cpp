#include "runyard/observability/metrics.hpp"
#include <cmath>
#include <gtest/gtest.h>
TEST(Metrics, ExportUsesBoundedMethodLabelsAndDurableSnapshots) {
  runyard::Metrics metrics;
  metrics.update({{"runs_queued", 12}, {"ready", 1}});
  metrics.rpc("Start", 0, .125);
  metrics.rpc("Start", 14, .2);
  auto text = metrics.render();
  EXPECT_NE(text.find("runyard_runs_queued 12"), std::string::npos);
  EXPECT_NE(text.find("runyard_rpc_duration_seconds_count{method=\"Start\"} 2"), std::string::npos);
  EXPECT_EQ(text.find("run_id="), std::string::npos);
}
TEST(Metrics, UnknownGpuAvailabilityIsNotReportedAsZero) {
  runyard::Metrics metrics;
  runyard::GpuCapacityTotals capacity;
  capacity.capacity = 4;
  capacity.reserved = 2;
  capacity.available = 2;
  capacity.fresh = true;
  metrics.gpus(capacity);
  EXPECT_NE(metrics.render().find("runyard_gpu_available_estimate 2"), std::string::npos);
  capacity.fresh = false;
  metrics.gpus(capacity);
  auto text = metrics.render();
  const std::string prefix = "\nrunyard_gpu_available_estimate ";
  auto position = text.find(prefix);
  ASSERT_NE(position, std::string::npos);
  EXPECT_TRUE(std::isnan(std::stod(text.substr(position + prefix.size()))));
  EXPECT_NE(text.find("runyard_gpu_inventory_fresh 0"), std::string::npos);
  EXPECT_EQ(text.find("uuid="), std::string::npos);
}
