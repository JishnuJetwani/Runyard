#include "runyard/runner/telemetry.hpp"
#include "runyard/domain/error.hpp"
#include "runyard/serialization/json.hpp"
#include <array>

namespace runyard {
namespace {
constexpr std::size_t buffer_limit = 4 * 1024 * 1024;
std::size_t size(const Telemetry &r) { return r.text.size() + r.name.size() + 128; }
std::int64_t timestamp() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}
std::string readable(const std::string &text) {
  auto result = Json::parse(Json(text).dump(-1, ' ', false, Json::error_handler_t::replace))
                    .get<std::string>();
  std::string clean;
  clean.reserve(result.size());
  for (char ch : result) {
    if (ch == '\0')
      clean += "\\0";
    else
      clean += ch;
  }
  return clean;
}
} // namespace
Reporter::Reporter(AttemptClient &client)
    : client_(client), thread_([this](std::stop_token stop) { send_loop(stop); }) {}
Reporter::~Reporter() {
  thread_.request_stop();
  changed_.notify_all();
  thread_.join();
}
void Reporter::enqueue(Telemetry record) {
  std::lock_guard lock(mutex_);
  if (failure_)
    std::rethrow_exception(failure_);
  if (bytes_ + size(record) > buffer_limit) {
    ++dropped_;
    return;
  }
  record.sequence = next_++;
  record.timestamp_ms = timestamp();
  bytes_ += size(record);
  queue_.push_back(std::move(record));
  changed_.notify_all();
}
void Reporter::log(const std::string &kind, const std::string &text) {
  auto clean = readable(text);
  for (std::size_t offset = 0; offset < clean.size();) {
    auto end = std::min(clean.size(), offset + 16000);
    while (end < clean.size() && (static_cast<unsigned char>(clean[end]) & 0xc0) == 0x80)
      --end;
    Telemetry record;
    record.kind = kind;
    record.text = clean.substr(offset, end - offset);
    enqueue(std::move(record));
    offset = end;
  }
}
void Reporter::metric(const std::string &name, std::int64_t step, double value) {
  Telemetry record;
  record.kind = "metric";
  record.name = name;
  record.step = step;
  record.value = value;
  record.sequence = 1;
  validate(record);
  enqueue(std::move(record));
}
void Reporter::send_loop(std::stop_token stop) {
  while (!stop.stop_requested()) {
    std::vector<Telemetry> batch;
    {
      std::unique_lock lock(mutex_);
      changed_.wait(lock, stop, [&] { return !queue_.empty(); });
      if (stop.stop_requested())
        break;
      std::size_t bytes = 0;
      for (const auto &r : queue_) {
        if (batch.size() >= 128 || bytes + size(r) > 256 * 1024)
          break;
        batch.push_back(r);
        bytes += size(r);
      }
    }
    try {
      auto ack = client_.report(batch);
      {
        std::lock_guard lock(mutex_);
        while (!queue_.empty() && queue_.front().sequence <= ack) {
          bytes_ -= size(queue_.front());
          queue_.pop_front();
        }
        if (dropped_ && bytes_ < buffer_limit - 1024) {
          Telemetry notice;
          notice.sequence = next_++;
          notice.kind = "notice";
          notice.text =
              "Telemetry buffer overflow: dropped " + std::to_string(dropped_) + " records";
          notice.timestamp_ms = timestamp();
          bytes_ += size(notice);
          queue_.push_back(std::move(notice));
          dropped_ = 0;
        }
      }
      changed_.notify_all();
    } catch (const Error &e) {
      if (e.code() != ErrorCode::unavailable) {
        std::lock_guard lock(mutex_);
        failure_ = std::current_exception();
        changed_.notify_all();
        return;
      }
      std::unique_lock lock(mutex_);
      changed_.wait_for(lock, stop, std::chrono::milliseconds(200), [] { return false; });
    } catch (...) {
      std::lock_guard lock(mutex_);
      failure_ = std::current_exception();
      changed_.notify_all();
      return;
    }
  }
}
std::int64_t Reporter::flush(std::chrono::steady_clock::time_point deadline) {
  std::unique_lock lock(mutex_);
  if (!changed_.wait_until(lock, deadline, [&] { return queue_.empty() || failure_; }))
    throw Error(ErrorCode::unavailable, "telemetry flush deadline exceeded");
  if (failure_)
    std::rethrow_exception(failure_);
  return next_ - 1;
}
void MetricReader::poll() {
  if (records_ >= 100000)
    return;
  input_.clear();
  // Reposition also clears the file buffer EOF state when the producer appends.
  input_.seekg(offset_);
  std::array<char, 4096> buffer{};
  for (int batch = 0; batch < 16; ++batch) {
    input_.read(buffer.data(), buffer.size());
    auto bytes = input_.gcount();
    offset_ += bytes;
    for (std::streamsize i = 0; i < bytes; ++i) {
      char ch = buffer[static_cast<std::size_t>(i)];
      if (ch == '\n') {
        if (!oversized_ && !partial_.empty()) {
          try {
            auto j = Json::parse(partial_);
            if (!j.at("step").is_number_integer() || !j.at("value").is_number() ||
                (j.at("step").is_number_unsigned() &&
                 j.at("step").get<std::uint64_t>() > INT64_MAX))
              throw Error(ErrorCode::invalid, "metric step/value types are invalid");
            reporter_.metric(j.at("name"), j.at("step"), j.at("value"));
          } catch (const Json::exception &) {
            reporter_.log("notice", "Ignored malformed metric record\n");
          } catch (const Error &e) {
            if (e.code() != ErrorCode::invalid)
              throw;
            reporter_.log("notice", "Ignored invalid metric record\n");
          }
        } else if (oversized_)
          reporter_.log("notice", "Ignored metric line exceeding 64 KiB\n");
        partial_.clear();
        oversized_ = false;
        if (++records_ >= 100000) {
          reporter_.log("notice", "Metric record limit reached\n");
          return;
        }
      } else if (!oversized_) {
        if (partial_.size() < 65536)
          partial_ += ch;
        else {
          partial_.clear();
          oversized_ = true;
        }
      }
    }
    if (bytes < static_cast<std::streamsize>(buffer.size()))
      break;
  }
}
} // namespace runyard
