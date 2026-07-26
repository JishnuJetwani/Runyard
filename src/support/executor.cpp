#include "runyard/support/executor.hpp"
#include <stdexcept>

namespace runyard {
Executor::Executor(std::size_t threads, std::size_t capacity) : capacity_(capacity) {
  if (!threads || !capacity)
    throw std::invalid_argument("executor sizes must be positive");
  for (std::size_t i = 0; i < threads; ++i)
    threads_.emplace_back([this] {
      while (true) {
        std::function<void()> task;
        {
          std::unique_lock lock(mutex_);
          ready_.wait(lock, [&] { return stopping_ || !tasks_.empty(); });
          if (tasks_.empty())
            return;
          task = std::move(tasks_.front());
          tasks_.pop();
        }
        task();
      }
    });
}
Executor::~Executor() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  ready_.notify_all();
  // Join before queue/mutex destruction, while allowing accepted work to drain.
  threads_.clear();
}
bool Executor::submit(std::function<void()> task) {
  {
    std::lock_guard lock(mutex_);
    if (stopping_ || tasks_.size() >= capacity_)
      return false;
    tasks_.push(std::move(task));
  }
  ready_.notify_one();
  return true;
}
} // namespace runyard
