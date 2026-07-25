#pragma once

#include <stdexcept>
#include <string>

namespace runyard {
enum class ErrorCode { invalid, not_found, conflict, unauthorized, unavailable, stale, exhausted };

class Error : public std::runtime_error {
public:
  Error(ErrorCode code, std::string message)
      : std::runtime_error(std::move(message)), code_(code) {}
  [[nodiscard]] ErrorCode code() const noexcept { return code_; }

private:
  ErrorCode code_;
};
} // namespace runyard
