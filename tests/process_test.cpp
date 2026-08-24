#include "runyard/runner/process.hpp"
#include <gtest/gtest.h>
#include <thread>

using namespace runyard;
TEST(Process, CapturesBothStreamsAndExitCode) {
  SteadyClock clock;
  PosixProcess process({"/bin/sh", "-c", "printf hello; printf error >&2; exit 7"},
                       {{"PATH", "/bin"}}, "/tmp", clock);
  std::string output, error;
  std::optional<int> status;
  auto consume = [&](const std::string &kind, const std::string &data) {
    (kind == "stdout" ? output : error) += data;
  };
  for (int i = 0; i < 100 && !status; ++i) {
    process.drain(consume);
    status = process.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  process.drain(consume);
  ASSERT_TRUE(status);
  EXPECT_EQ(*status, 7);
  EXPECT_EQ(output, "hello");
  EXPECT_EQ(error, "error");
}
TEST(Process, TerminatesTheProcessGroup) {
  SteadyClock clock;
  PosixProcess process({"/bin/sh", "-c", "sleep 60 & wait"}, {}, "/tmp", clock);
  process.stop(std::chrono::seconds(0));
  std::optional<int> status;
  for (int i = 0; i < 100 && !status; ++i) {
    status = process.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  ASSERT_TRUE(status);
  EXPECT_GE(*status, 128);
}

#include "runyard/domain/error.hpp"
#include "runyard/runner/environment.hpp"
#include <filesystem>
#include <fstream>
#include <unistd.h>
TEST(Environment, PreservesLibrariesWithoutInheritingCredentials) {
  RunSpec spec;
  spec.resources.gpu_count = 1;
  spec.environment = {{"EXPERIMENT", "yes"}, {"NVIDIA_VISIBLE_DEVICES", "all"}, {"HOME", "/bad"}};
  Environment inherited{{"PATH", "/opt/venv/bin:/usr/bin"},  {"LD_LIBRARY_PATH", "/opt/cuda/lib"},
                        {"NVIDIA_VISIBLE_DEVICES", "GPU-a"}, {"RUNYARD_CAPABILITY", "secret"},
                        {"AWS_SECRET_ACCESS_KEY", "secret"}, {"RUNYARD_DATABASE_URL", "secret"}};
  auto result = workload_environment(spec, inherited, "/work/attempt", "run", 2);
  EXPECT_EQ(result.at("PATH"), inherited.at("PATH"));
  EXPECT_EQ(result.at("LD_LIBRARY_PATH"), "/opt/cuda/lib");
  EXPECT_EQ(result.at("NVIDIA_VISIBLE_DEVICES"), "GPU-a");
  EXPECT_EQ(result.at("HOME"), "/work/attempt");
  EXPECT_FALSE(result.contains("RUNYARD_CAPABILITY"));
  EXPECT_FALSE(result.contains("AWS_SECRET_ACCESS_KEY"));
  EXPECT_FALSE(result.contains("RUNYARD_DATABASE_URL"));
  spec.resources.gpu_count = 0;
  EXPECT_EQ(workload_environment(spec, inherited, "/work", "run", 1).at("NVIDIA_VISIBLE_DEVICES"),
            "void");
}
TEST(Environment, CommandLookupUsesChildPathAndDirectory) {
  auto root = std::filesystem::temp_directory_path() / ("runyard-path-" + std::to_string(getpid()));
  std::filesystem::create_directories(root / "bin");
  std::filesystem::create_symlink("/bin/sh", root / "bin" / "workload-shell");
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() { std::filesystem::remove_all(path); }
  } cleanup{root};
  Environment environment{{"PATH", "bin"}};
  EXPECT_EQ(resolve_executable("workload-shell", environment, root),
            (root / "bin/workload-shell").string());
  EXPECT_THROW(resolve_executable("missing", environment, root), Error);
  SteadyClock clock;
  PosixProcess process({"workload-shell", "-c", "exit 23"}, environment, root.string(), clock);
  std::optional<int> status;
  for (int i = 0; i < 100 && !status; ++i) {
    status = process.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  ASSERT_TRUE(status);
  EXPECT_EQ(*status, 23);
}
