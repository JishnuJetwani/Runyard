#include "runyard/execution/kubernetes.hpp"
#include <gtest/gtest.h>

TEST(Kubernetes, FailureTargetIsNotYetAStoppedJob) {
  using runyard::Json;
  auto condition = [](const std::string &type, const std::string &status) {
    return Json{{"status", {{"conditions", {{{"type", type}, {"status", status}}}}}}};
  };
  EXPECT_FALSE(runyard::kubernetes_job_finished(Json::object()));
  EXPECT_FALSE(runyard::kubernetes_job_finished(condition("FailureTarget", "True")));
  EXPECT_FALSE(runyard::kubernetes_job_finished(condition("Failed", "False")));
  EXPECT_TRUE(runyard::kubernetes_job_finished(condition("Failed", "True")));
  EXPECT_TRUE(runyard::kubernetes_job_finished(condition("Complete", "True")));
}

TEST(Kubernetes, JobUsesSharedRunnerWithoutIndependentRetriesOrServiceCredentials) {
  runyard::Launch launch;
  launch.assignment.attempt.id = "attempt";
  launch.assignment.attempt.run_id = "run";
  launch.assignment.spec.image = "fixture@sha256:" + std::string(64, 'a');
  launch.assignment.spec.resources = {250, 128};
  launch.capability = "scoped";
  auto job = runyard::kubernetes_job({}, launch);
  auto pod = job["spec"]["template"]["spec"];
  EXPECT_EQ(job["spec"]["backoffLimit"], 0);
  EXPECT_EQ(job["spec"]["completions"], 1);
  EXPECT_EQ(pod["restartPolicy"], "Never");
  EXPECT_EQ(pod["automountServiceAccountToken"], false);
  auto container = pod["containers"][0];
  EXPECT_EQ(container["command"][0], "/usr/local/bin/runyard-runner");
  EXPECT_EQ(container["resources"]["limits"]["cpu"], "250m");
  EXPECT_EQ(container["resources"]["requests"]["memory"], "128Mi");
  EXPECT_EQ(pod["securityContext"]["runAsNonRoot"], true);
}
TEST(Kubernetes, GpuJobsRequestWholeDevicesAndPreservePluginVisibility) {
  runyard::Launch launch;
  launch.assignment.spec.resources = {2000, 8192, 2};
  runyard::KubernetesConfig config;
  config.gpu_runtime_class = "nvidia";
  auto pod = runyard::kubernetes_job(config, launch)["spec"]["template"]["spec"];
  auto resources = pod["containers"][0]["resources"];
  EXPECT_EQ(resources["requests"]["nvidia.com/gpu"], "2");
  EXPECT_EQ(resources["limits"]["nvidia.com/gpu"], "2");
  EXPECT_EQ(pod["runtimeClassName"], "nvidia");
  EXPECT_EQ(pod["nodeSelector"]["runyard.io/gpu-mode"], "exclusive");
  for (const auto &variable : pod["containers"][0]["env"])
    EXPECT_NE(variable["name"], "NVIDIA_VISIBLE_DEVICES");
  launch.assignment.spec.resources.gpu_count = 0;
  pod = runyard::kubernetes_job(config, launch)["spec"]["template"]["spec"];
  EXPECT_FALSE(pod.contains("runtimeClassName"));
  EXPECT_FALSE(pod.contains("nodeSelector"));
  EXPECT_FALSE(pod["containers"][0]["resources"]["limits"].contains("nvidia.com/gpu"));
}
