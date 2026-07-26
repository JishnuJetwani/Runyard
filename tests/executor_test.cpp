#include "runyard/support/executor.hpp"
#include <atomic>
#include <future>
#include <gtest/gtest.h>

TEST(Executor, RejectsOverflowAndDrainsAcceptedWork) {
  std::promise<void> entered, release;
  auto signal = release.get_future().share();
  std::atomic<int> count{};
  {
    runyard::Executor executor(1, 1);
    ASSERT_TRUE(executor.submit([&] {
      entered.set_value();
      signal.wait();
      ++count;
    }));
    entered.get_future().wait();
    EXPECT_TRUE(executor.submit([&] { ++count; }));
    EXPECT_FALSE(executor.submit([] {}));
    release.set_value();
  }
  EXPECT_EQ(count, 2);
}
