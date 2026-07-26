#include "runyard/postgres/leadership.hpp"

namespace runyard {
bool Leadership::refresh() {
  try {
    if (!connection_ || !connection_->is_open()) {
      ready_ = false;
      connection_ = std::make_unique<pqxx::connection>(dsn_);
    }
    pqxx::nontransaction tx(*connection_);
    if (!ready_)
      ready_ = tx.exec("SELECT pg_try_advisory_lock(72841902)")[0][0].as<bool>();
    else
      tx.exec("SELECT 1");
  } catch (...) {
    ready_ = false;
    connection_.reset();
  }
  return ready_;
}
} // namespace runyard
