#include "runyard/execution/kubernetes.hpp"
#include <gtest/gtest.h>

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
