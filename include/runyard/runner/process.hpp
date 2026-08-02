#pragma once
#include "runyard/domain/model.hpp"
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace runyard {
class Process {
public:
  virtual ~Process() = default;
  virtual void
  drain(const std::function<void(const std::string &, const std::string &)> &consume) = 0;
  virtual std::optional<int> poll() = 0;
  virtual void stop(std::chrono::seconds grace) = 0;
};
class PosixProcess final : public Process {
public:
  PosixProcess(const std::vector<std::string> &command,
               const std::map<std::string, std::string> &environment, const std::string &directory,
               const Clock &clock);
  ~PosixProcess() override;
  void drain(const std::function<void(const std::string &, const std::string &)> &) override;
  std::optional<int> poll() override;
  void stop(std::chrono::seconds grace) override;

private:
  int pid_{-1};
  int stdout_{-1};
  int stderr_{-1};
  const Clock &clock_;
  std::optional<int> status_;
  std::optional<std::chrono::steady_clock::time_point> kill_at_;
};
} // namespace runyard
