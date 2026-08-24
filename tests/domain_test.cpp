#include "runyard/domain/error.hpp"
#include "runyard/domain/model.hpp"
#include <climits>
#include <gtest/gtest.h>
#include <limits>

using namespace runyard;
namespace {
RunSpec example() {
  RunSpec spec;
  spec.name = "fixture";
  spec.image = "localhost:5001/fixture@sha256:" + std::string(64, 'a');
  spec.command = {"/bin/true"};
  return spec;
}
} // namespace
TEST(Domain, AcceptsPinnedImageAndDefaults) { EXPECT_NO_THROW(validate(example())); }
TEST(Domain, RejectsMutableImage) {
  auto s = example();
  s.image = "fixture:latest";
  EXPECT_THROW(validate(s), Error);
}
TEST(Domain, RejectsReservedEnvironment) {
  auto s = example();
  s.environment["RUNYARD_TOKEN"] = "x";
  EXPECT_THROW(validate(s), Error);
}
TEST(Domain, RejectsNonfiniteParameter) {
  auto s = example();
  s.parameters["x"] = std::numeric_limits<double>::infinity();
  EXPECT_THROW(validate(s), Error);
}
TEST(Domain, RejectsEmbeddedNullArgument) {
  auto s = example();
  s.command.push_back(std::string("a\0b", 3));
  EXPECT_THROW(validate(s), Error);
}
TEST(Domain, TerminalStatesCannotChange) {
  for (auto s : {RunStatus::succeeded, RunStatus::failed, RunStatus::cancelled}) {
    EXPECT_FALSE(transition_allowed(s, RunStatus::cancelled));
    EXPECT_FALSE(transition_allowed(s, RunStatus::running));
  }
}
TEST(Domain, SuccessRequiresFinalization) {
  EXPECT_FALSE(transition_allowed(RunStatus::running, RunStatus::succeeded));
  EXPECT_TRUE(transition_allowed(RunStatus::running, RunStatus::finalizing));
  EXPECT_TRUE(transition_allowed(RunStatus::finalizing, RunStatus::succeeded));
}
TEST(Domain, CapacityMustFitBothDimensions) {
  EXPECT_TRUE((Resources{500, 128}.fits({1000, 512})));
  EXPECT_FALSE((Resources{500, 1024}.fits({1000, 512})));
  EXPECT_FALSE((Resources{2000, 128}.fits({1000, 512})));
}
TEST(Domain, RetryIsBoundedAndFailureSpecific) {
  RetryPolicy policy;
  EXPECT_TRUE(should_retry(policy, 1, Failure::infrastructure));
  EXPECT_FALSE(should_retry(policy, 3, Failure::infrastructure));
  EXPECT_FALSE(should_retry(policy, 1, Failure::exit_error));
  EXPECT_FALSE(should_retry(policy, 1, Failure::timeout));
  EXPECT_FALSE(should_retry(policy, 1, Failure::cancelled));
  policy.retry_exit = true;
  EXPECT_TRUE(should_retry(policy, 1, Failure::exit_error));
}
TEST(Domain, BackoffCapsWithoutOverflow) {
  EXPECT_EQ(retry_delay(1), 5);
  EXPECT_EQ(retry_delay(2), 10);
  EXPECT_EQ(retry_delay(100000), 60);
  EXPECT_EQ(retry_delay(4, 3, 20), 20);
  EXPECT_EQ(retry_delay(3, 3, 100), 12);
  EXPECT_EQ(retry_delay(100000, INT_MAX, INT_MAX), INT_MAX);
}
TEST(Domain, SweepExpandsDeterministically) {
  auto result = expand_sweep(
      example(), {{"seed", {std::int64_t{1}, std::int64_t{2}}}, {"rate", {0.1, 0.2, 0.3}}});
  ASSERT_EQ(result.size(), 6);
  EXPECT_EQ(std::get<double>(result[0].parameters.at("rate")), 0.1);
  EXPECT_EQ(std::get<std::int64_t>(result[1].parameters.at("seed")), 2);
}
TEST(Domain, SweepRejectsOversizeBeforeExpansion) {
  EXPECT_THROW(
      expand_sweep(example(), {{"a", std::vector<Scalar>(100)}, {"b", std::vector<Scalar>(100)}}),
      Error);
}
TEST(Domain, ArtifactPathsCannotEscape) {
  for (auto path : {"../key", "/etc/passwd", "a/../key", "a//b", "a/", "a\\b", "."})
    EXPECT_THROW(validate_relative_path(path), Error);
  EXPECT_NO_THROW(validate_relative_path("checkpoints/epoch-1.bin"));
}
TEST(Domain, GpusMustFitAlongsideCpuAndMemory) {
  EXPECT_TRUE((Resources{1000, 512, 2}.fits({4000, 4096, 2})));
  EXPECT_FALSE((Resources{1000, 512, 2}.fits({4000, 4096, 1})));
  EXPECT_FALSE((Resources{1000, 512, 1}.fits({500, 4096, 4})));
  EXPECT_TRUE((Resources{1000, 512}.fits({4000, 4096, 0})));
}
TEST(Domain, GpuRequestsAreBoundedWholeDeviceCounts) {
  auto spec = example();
  for (int count : {0, 1, 8, 64}) {
    spec.resources.gpu_count = count;
    EXPECT_NO_THROW(validate(spec));
    EXPECT_EQ(
        expand_sweep(spec, {{"seed", {std::int64_t{1}, std::int64_t{2}}}})[1].resources.gpu_count,
        count);
  }
  for (int count : {-1, 65, INT_MAX}) {
    spec.resources.gpu_count = count;
    EXPECT_THROW(validate(spec), Error);
  }
}
