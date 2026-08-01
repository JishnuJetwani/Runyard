#include "runyard/serialization/json.hpp"
#include "runyard/support/config.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

int main() {
  using namespace runyard;
  try {
    auto parameters = Json::parse(read_file(env("RUNYARD_PARAMETERS_PATH")));
    auto mode = parameters.value("mode", "success");
    int steps = parameters.value("steps", 5), delay = parameters.value("delay_ms", 20),
        seed = parameters.value("seed", 1);
    std::ofstream metrics(env("RUNYARD_METRICS_PATH"), std::ios::app);
    if (mode == "hang")
      while (true)
        std::this_thread::sleep_for(std::chrono::seconds(1));
    for (int step = 0; step < steps; ++step) {
      std::cout << "step " << step << " seed " << seed << std::endl;
      metrics << Json{{"name", "score"}, {"step", step}, {"value", (seed + step) * 0.125}}.dump()
              << std::endl;
      if (mode == "noisy")
        std::cout << std::string(200000, 'x') << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
    std::filesystem::create_directories(env("RUNYARD_OUTPUT_DIR"));
    std::ofstream(std::filesystem::path(env("RUNYARD_OUTPUT_DIR")) / "result.json")
        << Json{{"seed", seed}, {"steps", steps}, {"score", (seed + steps - 1) * 0.125}}.dump();
    if (mode == "fail" || (mode == "fail_once" && env_int("RUNYARD_ATTEMPT_NUMBER", 1) == 1))
      return 7;
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
