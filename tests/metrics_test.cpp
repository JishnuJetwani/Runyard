#include "runyard/observability/metrics.hpp"
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
