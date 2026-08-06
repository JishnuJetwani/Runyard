#pragma once
#include "runyard/runner/client.hpp"
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>

namespace runyard {
class Reporter {
public:
  explicit Reporter(AttemptClient &client);
  ~Reporter();
  void log(const std::string &kind, const std::string &text);
  void metric(const std::string &name, std::int64_t step, double value);
  std::int64_t flush(std::chrono::steady_clock::time_point deadline);

private:
  void enqueue(Telemetry record);
  void send_loop(std::stop_token stop);
  AttemptClient &client_;
  std::mutex mutex_;
  std::condition_variable_any changed_;
  std::deque<Telemetry> queue_;
  std::size_t bytes_{};
  std::uint64_t dropped_{};
  std::int64_t next_{1};
  std::exception_ptr failure_;
  std::jthread thread_;
};
class MetricReader {
public:
  MetricReader(const std::string &path, Reporter &reporter) : input_(path), reporter_(reporter) {}
  void poll();

private:
  std::ifstream input_;
  std::streamoff offset_{};
  Reporter &reporter_;
  std::string partial_;
  bool oversized_{};
  std::size_t records_{};
};
} // namespace runyard
