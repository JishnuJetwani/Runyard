#pragma once
#include "runyard/application/repository.hpp"
#include "runyard/postgres/pool.hpp"

namespace runyard {
class PostgresStore final : public Repository {
public:
  explicit PostgresStore(ConnectionPool &pool, Timing timing = {}) : pool_(pool), timing_(timing) {}
  Run submit(const RunSpec &, const std::string &key, const std::string &fingerprint) override;
  Run get_run(const std::string &id) override;
  std::vector<Run> list_runs(int limit, const std::string &after,
                             const std::string &status) override;
  std::vector<Attempt> attempts(const std::string &run_id) override;
  std::vector<Event> events(const std::string &run_id, std::int64_t after, int limit) override;

private:
  ConnectionPool &pool_;
  Timing timing_;
};
namespace pg {
using RowView = decltype(std::declval<const pqxx::result &>()[0]);
using FieldView = decltype(std::declval<const RowView &>()[0]);
Run run(const RowView &row);
Attempt attempt(const RowView &row);
void event(pqxx::work &tx, const std::string &run_id, const std::string &kind,
           const std::string &detail);
std::string text(const FieldView &field);
} // namespace pg
} // namespace runyard
