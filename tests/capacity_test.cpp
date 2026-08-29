#include "runyard/domain/error.hpp"
#include "runyard/execution/kubernetes_capacity.hpp"
#include <gtest/gtest.h>
using namespace runyard;
namespace {
class TestClock : public Clock {
public:
  std::chrono::steady_clock::time_point value{};
  std::chrono::steady_clock::time_point now() const override { return value; }
};
Json container(int count, bool sidecar = false) {
  Json c = {{"resources", {{"requests", {{"nvidia.com/gpu", std::to_string(count)}}}}}};
  if (sidecar)
    c["restartPolicy"] = "Always";
  return c;
}
Json node(const std::string &name = "gpu-node") {
  return {{"metadata", {{"name", name}, {"labels", {{"runyard.io/gpu-mode", "exclusive"}}}}},
          {"status",
           {{"capacity", {{"nvidia.com/gpu", "8"}}},
            {"allocatable", {{"nvidia.com/gpu", "8"}}},
            {"conditions", Json::array({{{"type", "Ready"}, {"status", "True"}}})}}}};
}
Json pod(const std::string &phase, const std::string &node_name, int count) {
  return {{"metadata", {{"namespace", "another-team"}}},
          {"status", {{"phase", phase}}},
          {"spec", {{"nodeName", node_name}, {"containers", Json::array({container(count)})}}}};
}
HttpResult page(Json items, const std::string &next = "") {
  return {200, Json{{"metadata", {{"continue", next}}}, {"items", std::move(items)}}.dump()};
}
} // namespace
TEST(Capacity, ParsesWholeDeviceQuantities) {
  for (const auto &value : {Json(1), Json("1"), Json("1000m"), Json("1e0")})
    EXPECT_EQ(gpu_quantity(value), 1);
  EXPECT_EQ(gpu_quantity("1k"), 1000);
  for (const auto &value : {Json(-1), Json("0.5"), Json("huge"), Json(1.5), Json("1e1000")})
    EXPECT_THROW(gpu_quantity(value), std::exception);
}
TEST(Capacity, AccountsForRestartableInitContainersInOrder) {
  auto p = pod("Running", "gpu-node", 2);
  p["spec"]["initContainers"] =
      Json::array({container(1, true), container(4), container(2, true), container(1)});
  EXPECT_EQ(pod_gpu_request(p), 5);
  p["spec"]["initContainers"] = Json::array({container(1, true), container(6)});
  EXPECT_EQ(pod_gpu_request(p), 7);
  p["spec"]["overhead"] = {{"nvidia.com/gpu", "1"}};
  EXPECT_EQ(pod_gpu_request(p), 8);
}
TEST(Capacity, CordonAndReadinessControlEligibility) {
  auto n = node();
  EXPECT_TRUE(gpu_node_capacity(n).eligible);
  n["spec"]["unschedulable"] = true;
  EXPECT_FALSE(gpu_node_capacity(n).eligible);
  n["spec"]["unschedulable"] = false;
  n["status"]["conditions"][0]["status"] = "False";
  EXPECT_FALSE(gpu_node_capacity(n).eligible);
}
TEST(Capacity, PagesClusterResourcesAndKeepsLastObservationOnFailure) {
  TestClock clock;
  bool fail = false;
  int pages = 0;
  KubernetesCapacity capacity({}, clock, [&](const std::string &path) {
    if (fail)
      return HttpResult{403, ""};
    if (path.starts_with("/api/v1/nodes")) {
      ++pages;
      if (path.ends_with("continue=next"))
        return page(Json::array({node("other-node")}));
      return page(Json::array({node()}), "next");
    }
    EXPECT_TRUE(path.starts_with("/api/v1/pods?"));
    auto terminating = pod("Running", "gpu-node", 1);
    terminating["metadata"]["deletionTimestamp"] = "2026-09-18T00:00:00Z";
    return page(Json::array({pod("Running", "gpu-node", 2), pod("Pending", "gpu-node", 1),
                             terminating, pod("Succeeded", "gpu-node", 8),
                             pod("Failed", "gpu-node", 8), pod("Pending", "", 3)}));
  });
  EXPECT_FALSE(capacity.snapshot().age_seconds);
  capacity.refresh();
  EXPECT_EQ(pages, 2);
  auto current = capacity.snapshot();
  ASSERT_EQ(current.nodes.size(), 2);
  EXPECT_EQ(current.nodes[0].reserved, 4);
  EXPECT_EQ(current.pending, 3);
  EXPECT_TRUE(current.fresh);
  clock.value += std::chrono::seconds(45);
  EXPECT_FALSE(capacity.snapshot().fresh);
  fail = true;
  EXPECT_THROW(capacity.refresh(), Error);
  EXPECT_EQ(capacity.snapshot().nodes[0].reserved, 4);
  EXPECT_FALSE(capacity.snapshot().fresh);
}
TEST(Capacity, RepeatedPaginationCursorFailsWithoutPublishingPartialSnapshot) {
  TestClock clock;
  KubernetesCapacity capacity({}, clock,
                              [](const std::string &) { return page(Json::array(), "same"); });
  EXPECT_THROW(capacity.refresh(), Error);
  EXPECT_FALSE(capacity.snapshot().age_seconds);
  EXPECT_TRUE(capacity.snapshot().nodes.empty());
}
