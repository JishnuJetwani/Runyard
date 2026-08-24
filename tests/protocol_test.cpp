#include "runyard.pb.h"
#include <gtest/gtest.h>
namespace wire = runyard::rpc::v1;
TEST(Protocol, LegacyRegistrationHasNoGpuCapability) {
  wire::RegisterRequest legacy;
  legacy.mutable_worker()->set_id("cpu");
  legacy.set_cpu_millis(1000);
  wire::RegisterRequest decoded;
  ASSERT_TRUE(decoded.ParseFromString(legacy.SerializeAsString()));
  EXPECT_FALSE(decoded.gpu_capable());
  EXPECT_TRUE(decoded.engine_id().empty());
}
TEST(Protocol, AssignmentRetainsExactGpuUuids) {
  wire::WorkAssignment assignment;
  assignment.add_gpu_uuids("GPU-a");
  assignment.add_gpu_uuids("GPU-b");
  wire::WorkAssignment decoded;
  ASSERT_TRUE(decoded.ParseFromString(assignment.SerializeAsString()));
  EXPECT_EQ(decoded.gpu_uuids_size(), 2);
  EXPECT_EQ(decoded.gpu_uuids(1), "GPU-b");
}
