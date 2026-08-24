#include "runyard/domain/error.hpp"
#include "runyard/nvidia/inventory.hpp"
#include <cstdlib>
#include <gtest/gtest.h>
using namespace runyard;
class Nvidia : public ::testing::Test {
protected:
  void TearDown() override { unsetenv("RUNYARD_NVML_FIXTURE_MODE"); }
};
TEST_F(Nvidia, DiscoversStableUuidsAndFiltersAllowlist) {
  NvidiaInventory inventory({}, NVML_FIXTURE);
  auto first = inventory.discover();
  ASSERT_EQ(first.size(), 2);
  EXPECT_EQ(first[0].memory_mib, 24576);
  setenv("RUNYARD_NVML_FIXTURE_MODE", "reverse", 1);
  EXPECT_EQ(inventory.discover()[0].uuid, first[0].uuid);
  NvidiaInventory selected({"GPU-2"}, NVML_FIXTURE);
  ASSERT_EQ(selected.discover().size(), 1);
  EXPECT_EQ(selected.discover()[0].uuid, "GPU-2");
}
TEST_F(Nvidia, MissingLibraryAndInitializationFailExplicitly) {
  NvidiaInventory missing({}, "/does/not/exist");
  EXPECT_THROW(missing.discover(), Error);
  setenv("RUNYARD_NVML_FIXTURE_MODE", "init-error", 1);
  NvidiaInventory inventory({}, NVML_FIXTURE);
  EXPECT_THROW(inventory.discover(), Error);
}
TEST_F(Nvidia, MigAndMetadataErrorsExcludeDevices) {
  NvidiaInventory inventory({}, NVML_FIXTURE);
  for (auto mode : {"mig", "query-error"}) {
    setenv("RUNYARD_NVML_FIXTURE_MODE", mode, 1);
    auto result = inventory.discover();
    ASSERT_EQ(result.size(), 2);
    EXPECT_FALSE(result[0].eligible);
  }
  setenv("RUNYARD_NVML_FIXTURE_MODE", "permission", 1);
  EXPECT_EQ(inventory.discover().size(), 1);
  NvidiaInventory selected({"GPU-2"}, NVML_FIXTURE);
  EXPECT_THROW(selected.discover(), Error);
}
TEST_F(Nvidia, AllowlistRejectsIndicesDuplicatesAndEmptyEntries) {
  EXPECT_EQ(gpu_allowlist("GPU-1,GPU-2").size(), 2);
  EXPECT_TRUE(gpu_allowlist("").empty());
  for (auto value : {"0", "GPU-1,GPU-1", "GPU-1,", ",GPU-1", "GPU-1, GPU-2"})
    EXPECT_THROW(gpu_allowlist(value), Error);
}
