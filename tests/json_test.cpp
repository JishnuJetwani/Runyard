#include "runyard/domain/error.hpp"
#include "runyard/serialization/json.hpp"
#include <gtest/gtest.h>
using namespace runyard;
TEST(Serialization, RejectsFractionalAndOverflowingIntegerFields) {
  Json spec = {
      {"name", "test"}, {"image", "fixture@sha256:" + std::string(64, 'a')}, {"command", {"true"}}};
  auto fractional = spec;
  fractional["resources"] = {{"cpu_millis", 1.5}};
  EXPECT_THROW(decode_spec(fractional), Error);
  auto overflow = spec;
  overflow["timeout_seconds"] = std::uint64_t{1} << 40;
  EXPECT_THROW(decode_spec(overflow), Error);
  EXPECT_NO_THROW(decode_spec(spec));
}
TEST(Serialization, RejectsUnknownNestedFields) {
  Json spec = {{"name", "test"},
               {"image", "fixture@sha256:" + std::string(64, 'a')},
               {"command", {"true"}},
               {"retry", {{"retry_exist", true}}}};
  EXPECT_THROW(decode_spec(spec), Error);
}
TEST(Serialization, ZeroGpuCountPreservesCpuCanonicalEncoding) {
  Json json = {
      {"name", "cpu"}, {"image", "fixture@sha256:" + std::string(64, 'a')}, {"command", {"true"}}};
  auto cpu = encode(decode_spec(json)).dump();
  json["resources"] = {{"gpu_count", 0}};
  EXPECT_EQ(encode(decode_spec(json)).dump(), cpu);
  EXPECT_FALSE(encode(decode_spec(json))["resources"].contains("gpu_count"));
  for (auto value : {Json(-1), Json(65), Json(1.5), Json(std::uint64_t{1} << 40)}) {
    json["resources"]["gpu_count"] = value;
    EXPECT_THROW(decode_spec(json), Error);
  }
  json["resources"]["gpu_count"] = 2;
  EXPECT_EQ(decode_spec(json).resources.gpu_count, 2);
  EXPECT_EQ(encode(decode_spec(json))["resources"]["gpu_count"], 2);
}
TEST(Serialization, HistoricalNvidiaEnvironmentRemainsReadable) {
  Json json = {{"name", "old"},
               {"image", "fixture@sha256:" + std::string(64, 'a')},
               {"command", {"true"}},
               {"environment", {{"NVIDIA_VISIBLE_DEVICES", "all"}}}};
  EXPECT_NO_THROW(decode_spec(json));
  EXPECT_THROW(validate_submission(decode_spec(json)), Error);
}
