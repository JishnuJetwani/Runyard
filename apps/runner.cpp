#include "runyard/runner/engine.hpp"
#include "runyard/support/config.hpp"
#include <csignal>
#include <iostream>

namespace {
// Only the POSIX signal boundary uses global state; handlers cannot call C++ services.
volatile std::sig_atomic_t interrupted = 0;
void interrupt(int) { interrupted = 1; }
} // namespace
int main() {
  std::signal(SIGTERM, interrupt);
  std::signal(SIGINT, interrupt);
  try {
    runyard::RunnerConfig config{runyard::env("RUNYARD_ATTEMPT_ID"),
                                 runyard::env("RUNYARD_RUN_ID"),
                                 runyard::env_int("RUNYARD_GENERATION", 1),
                                 runyard::env("RUNYARD_COORDINATOR", "localhost:9090"),
                                 runyard::env("RUNYARD_CAPABILITY"),
                                 runyard::env("RUNYARD_TLS_CA"),
                                 runyard::env("RUNYARD_WORK_ROOT", "/tmp/runyard"),
                                 runyard::env("RUNYARD_PROFILE") == "development"};
    if (config.attempt_id.empty() || config.capability.empty())
      throw std::runtime_error("attempt identity and capability required");
    return runyard::run_attempt(config, [] { return interrupted != 0; });
  } catch (const std::exception &e) {
    std::cerr << "runner: " << e.what() << '\n';
    return 1;
  }
}
