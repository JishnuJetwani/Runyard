#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace runyard {
class Executor {
public:
  explicit Executor(std::size_t threads = 8, std::size_t capacity = 256);
  ~Executor();
  bool submit(std::function<void()> task);

private:
  std::mutex mutex_;
  std::condition_variable ready_;
  std::queue<std::function<void()>> tasks_;
  std::size_t capacity_;
  bool stopping_{};
  std::vector<std::jthread> threads_;
};
} // namespace runyard
