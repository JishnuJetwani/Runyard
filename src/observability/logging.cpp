#include "runyard/observability/metrics.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/formatter.h>
#include <spdlog/spdlog.h>

namespace runyard {
namespace {
class JsonFormatter final : public spdlog::formatter {
public:
  void format(const spdlog::details::log_msg &record, spdlog::memory_buf_t &destination) override {
    std::string message(record.payload.data(), record.payload.size());
    auto value = nlohmann::json::parse(message, nullptr, false);
    if (!value.is_object())
      value = {{"message", message}};
    value["level"] = std::string(spdlog::level::to_string_view(record.level).data(),
                                 spdlog::level::to_string_view(record.level).size());
    value["timestamp_ms"] =
        std::chrono::duration_cast<std::chrono::milliseconds>(record.time.time_since_epoch())
            .count();
    auto line = value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";
    destination.append(line.data(), line.data() + line.size());
  }
  std::unique_ptr<spdlog::formatter> clone() const override {
    return std::make_unique<JsonFormatter>();
  }
};
} // namespace
void structured_logging() { spdlog::set_formatter(std::make_unique<JsonFormatter>()); }
} // namespace runyard
