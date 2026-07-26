#include "runyard/postgres/pool.hpp"
#include "runyard/domain/error.hpp"
#include "runyard/support/crypto.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace runyard {
ConnectionPool::ConnectionPool(std::string dsn, std::size_t size) : dsn_(std::move(dsn)) {
  if (size == 0)
    throw Error(ErrorCode::invalid, "database pool must not be empty");
  for (std::size_t i = 0; i < size; ++i)
    connections_.push_back(std::make_unique<pqxx::connection>(dsn_));
}
ConnectionPool::Lease::Lease(ConnectionPool &pool, std::unique_ptr<pqxx::connection> connection)
    : pool_(pool), connection_(std::move(connection)) {}
ConnectionPool::Lease::~Lease() {
  {
    std::lock_guard lock(pool_.mutex_);
    pool_.connections_.push_back(std::move(connection_));
  }
  pool_.available_.notify_one();
}
ConnectionPool::Lease ConnectionPool::acquire() {
  std::unique_lock lock(mutex_);
  if (!available_.wait_for(lock, std::chrono::seconds(5), [&] { return !connections_.empty(); }))
    throw Error(ErrorCode::unavailable, "database pool is busy");
  auto connection = std::move(connections_.back());
  connections_.pop_back();
  lock.unlock();
  try {
    if (!connection || !connection->is_open())
      connection = std::make_unique<pqxx::connection>(dsn_);
  } catch (...) {
    {
      std::lock_guard guard(mutex_);
      connections_.push_back(nullptr);
    }
    available_.notify_one();
    throw;
  }
  return Lease(*this, std::move(connection));
}
void migrate(ConnectionPool &pool, const std::string &directory) {
  auto connection = pool.acquire();
  pqxx::work tx(connection.get());
  tx.exec("SELECT pg_advisory_xact_lock(72841901)");
  tx.exec("CREATE TABLE IF NOT EXISTS schema_migrations (name text PRIMARY KEY, checksum text NOT "
          "NULL, applied_at timestamptz NOT NULL DEFAULT clock_timestamp())");
  std::vector<std::filesystem::path> files;
  for (const auto &entry : std::filesystem::directory_iterator(directory))
    if (entry.path().extension() == ".sql")
      files.push_back(entry.path());
  std::sort(files.begin(), files.end());
  for (const auto &path : files) {
    std::ifstream input(path);
    std::string sql((std::istreambuf_iterator<char>(input)), {});
    auto checksum = sha256(sql);
    auto name = path.filename().string();
    auto existing =
        tx.exec("SELECT checksum FROM schema_migrations WHERE name=$1", pqxx::params{name});
    if (!existing.empty()) {
      if (existing[0][0].as<std::string>() != checksum)
        throw Error(ErrorCode::conflict, "applied migration changed: " + name);
      continue;
    }
    tx.exec(sql);
    tx.exec("INSERT INTO schema_migrations(name,checksum) VALUES($1,$2)",
            pqxx::params{name, checksum});
  }
  tx.commit();
}
} // namespace runyard
