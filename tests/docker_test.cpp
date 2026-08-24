#include "runyard/domain/error.hpp"
#include "runyard/execution/docker.hpp"
#include <gtest/gtest.h>
using namespace runyard;
namespace {
DockerConfig config{"/socket", "net", "worker", "server:9090", "", true};
Launch gpu_launch() {
  Launch launch;
  launch.assignment.spec.resources.gpu_count = 2;
  launch.assignment.spec.image = "fixture@sha256:" + std::string(64, 'a');
  launch.assignment.attempt.id = "attempt";
  launch.assignment.attempt.gpu_allocations = {{{"GPU-a", "", 0, true, ""}, "", ""},
                                               {{"GPU-b", "", 0, true, ""}, "", ""}};
  return launch;
}
Json inspect(const Json &spec) { return {{"Config", spec}, {"HostConfig", spec["HostConfig"]}}; }
} // namespace
TEST(Docker, RequestsExactGpuUuidsWithCpuMemoryLimits) {
  auto spec = docker_container_spec(config, gpu_launch());
  auto request = spec["HostConfig"]["DeviceRequests"][0];
  EXPECT_EQ(request["DeviceIDs"], Json::array({"GPU-a", "GPU-b"}));
  EXPECT_EQ(request["Count"], 0);
  EXPECT_EQ(spec["HostConfig"]["Memory"], 512 * 1024 * 1024);
  EXPECT_TRUE(docker_container_matches(inspect(spec), config, gpu_launch()));
}
TEST(Docker, RejectsIncompleteOrDuplicateDeviceAssignments) {
  auto launch = gpu_launch();
  launch.assignment.attempt.gpu_allocations.pop_back();
  EXPECT_THROW(docker_container_spec(config, launch), Error);
  launch = gpu_launch();
  launch.assignment.attempt.gpu_allocations[1] = launch.assignment.attempt.gpu_allocations[0];
  EXPECT_THROW(docker_container_spec(config, launch), Error);
}
TEST(Docker, ExistingContainerMustMatchGpuAllocationAndWorker) {
  auto launch = gpu_launch();
  auto actual = inspect(docker_container_spec(config, launch));
  actual["HostConfig"]["DeviceRequests"][0]["DeviceIDs"] = {"GPU-c", "GPU-d"};
  EXPECT_FALSE(docker_container_matches(actual, config, launch));
  actual = inspect(docker_container_spec(config, launch));
  actual["Config"]["Labels"]["runyard.worker"] = "other";
  EXPECT_FALSE(docker_container_matches(actual, config, launch));
}
TEST(Docker, CpuContainersDisableNvidiaExposure) {
  Launch launch;
  auto spec = docker_container_spec(config, launch);
  EXPECT_FALSE(spec["HostConfig"].contains("DeviceRequests"));
  auto environment = spec["Env"].get<std::vector<std::string>>();
  EXPECT_NE(std::find(environment.begin(), environment.end(), "NVIDIA_VISIBLE_DEVICES=void"),
            environment.end());
}
