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
