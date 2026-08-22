#include "runyard/domain/error.hpp"
#include "runyard/execution/docker.hpp"
#include "runyard/execution/kubernetes.hpp"
#include <iostream>

int main(int argc, char **argv) {
  try {
    if (argc < 3)
      return 2;
    std::unique_ptr<runyard::ExecutionBackend> backend;
    if (std::string(argv[1]) == "docker")
      backend = std::make_unique<runyard::DockerBackend>(
          runyard::DockerConfig{argv[2], "runyard", "test-worker", "server:9090", "", true});
    else {
      runyard::KubernetesConfig config;
      config.api = argv[2];
      config.token_file = argv[3];
      config.api_ca = "";
      config.development = true;
      backend = std::make_unique<runyard::KubernetesBackend>(config);
    }
    runyard::Launch launch;
    launch.assignment.attempt.id = "test-attempt";
    launch.assignment.attempt.run_id = "test-run";
    launch.assignment.spec.image = "fixture@sha256:" + std::string(64, 'a');
    launch.capability = "attempt-token";
    bool failed = false;
    try {
      backend->ensure(launch);
    } catch (const runyard::Error &) {
      failed = true;
    }
    if (!failed)
      throw std::runtime_error("fault was not observed");
    if (backend->ensure(launch) != "runtime-uid")
      throw std::runtime_error("ambiguous creation was not recovered");
    if (backend->ensure(launch) != "runtime-uid")
      throw std::runtime_error("repeat launch changed identity");
    if (backend->inventory() != std::vector<std::string>{"test-attempt"})
      throw std::runtime_error("inventory mismatch");
    if (backend->has_stopped("test-attempt"))
      throw std::runtime_error("active runtime was classified as stopped");
    backend->remove("test-attempt");
    backend->remove("test-attempt");
    if (!backend->inventory().empty())
      throw std::runtime_error("cleanup failed");
    if (!backend->has_stopped("test-attempt"))
      throw std::runtime_error("missing runtime was classified as active");
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
