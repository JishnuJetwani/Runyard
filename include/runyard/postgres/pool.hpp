#pragma once
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <pqxx/pqxx>
#include <string>
#include <vector>

namespace runyard {
class ConnectionPool {
public:
  class Lease {
  public:
    Lease(ConnectionPool &pool, std::unique_ptr<pqxx::connection> connection);
    ~Lease();
    Lease(const Lease &) = delete;
    Lease &operator=(const Lease &) = delete;
    pqxx::connection &get() { return *connection_; }

  private:
    ConnectionPool &pool_;
    std::unique_ptr<pqxx::connection> connection_;
  };
  explicit ConnectionPool(std::string dsn, std::size_t size = 8);
  Lease acquire();
  std::map<std::string, double> statistics();
  const std::string &dsn() const { return dsn_; }

private:
  std::size_t capacity_;
  std::atomic<int> waiting_{0};
  std::atomic<int> timeouts_{0};
  std::string dsn_;
  std::mutex mutex_;
  std::condition_variable available_;
  std::vector<std::unique_ptr<pqxx::connection>> connections_;
};
void migrate(ConnectionPool &pool, const std::string &directory);
} // namespace runyard
