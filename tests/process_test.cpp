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
