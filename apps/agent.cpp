#include "runyard/agent/agent.hpp"
#include "runyard/execution/docker.hpp"
#include "runyard/observability/metrics.hpp"
#include "runyard/support/config.hpp"
#include <csignal>
#include <iostream>

namespace {
volatile std::sig_atomic_t interrupted = 0;
void interrupt(int) { interrupted = 1; }
} // namespace
int main() {
  runyard::structured_logging();
  std::signal(SIGTERM, interrupt);
  std::signal(SIGINT, interrupt);
  try {
    runyard::AgentConfig config{runyard::env("RUNYARD_WORKER_ID"),
                                runyard::env("RUNYARD_COORDINATOR", "localhost:9090"),
                                runyard::env("RUNYARD_WORKER_TOKEN"),
                                runyard::env("RUNYARD_TLS_CA"),
                                {runyard::env_int("RUNYARD_CPU_MILLIS", 4000),
                                 runyard::env_int("RUNYARD_MEMORY_MIB", 4096)},
                                runyard::env("RUNYARD_PROFILE") == "development",
                                {}};
    if (config.id.empty() || config.token.empty())
      throw std::runtime_error("worker identity and token required");
    runyard::DockerBackend docker({runyard::env("RUNYARD_DOCKER_SOCKET", "/var/run/docker.sock"),
                                   runyard::env("RUNYARD_DOCKER_NETWORK", "runyard"), config.id,
                                   runyard::env("RUNYARD_RUNNER_COORDINATOR", config.endpoint),
                                   runyard::env("RUNYARD_DOCKER_CA_HOST_PATH"),
                                   config.development});
    auto mode = runyard::env("RUNYARD_GPU_MODE", "disabled");
    if (mode != "disabled" && mode != "nvidia")
      throw std::runtime_error("RUNYARD_GPU_MODE must be disabled or nvidia");
    std::unique_ptr<runyard::NvidiaInventory> gpus;
    if (mode == "nvidia") {
      gpus = std::make_unique<runyard::NvidiaInventory>(
          runyard::gpu_allowlist(runyard::env("RUNYARD_GPU_UUIDS")));
      config.gpu = {docker.engine_id(), true};
    }
    runyard::run_agent(config, docker, [] { return interrupted != 0; }, gpus.get());
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "agent: " << e.what() << '\n';
    return 1;
  }
}
