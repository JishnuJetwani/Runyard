#pragma once
#include <atomic>
#include <memory>
#include <pqxx/pqxx>
#include <string>

namespace runyard {
class Leadership {
public:
  explicit Leadership(std::string dsn) : dsn_(std::move(dsn)) {}
  bool refresh();
  bool ready() const { return ready_.load(); }

private:
  std::string dsn_;
  std::unique_ptr<pqxx::connection> connection_;
  std::atomic<bool> ready_{false};
};
} // namespace runyard
