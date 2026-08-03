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
  void register_worker(const std::string &, const std::string &, Resources) override;
  void worker_heartbeat(const std::string &, const std::string &) override;
  std::vector<Worker> workers() override;
  std::optional<Assignment> assign(const std::string &, const std::string &) override;
  void runtime_report(const std::string &, const std::string &, const std::string &,
                      const std::string &, bool) override;
  std::vector<Attempt> cleanup(const std::string &, const std::string &) override;
  Assignment start(const std::string &, int, const std::string &) override;
  void heartbeat(const std::string &, int, const std::string &) override;
  void begin_finalization(const std::string &, int, const std::string &) override;
  void finish(const std::string &, int, const std::string &, int, const std::string &,
              std::int64_t) override;

  std::int64_t report(const std::string &, int, const std::string &,
                      const std::vector<Telemetry> &) override;
  std::vector<Telemetry> telemetry(const std::string &, const std::string &, std::int64_t, int,
                                   const std::string &, const std::string &) override;

  void verify_owner(const std::string &, int, const std::string &) override;
  Artifact publish_artifact(const Artifact &, int, const std::string &) override;
  std::vector<Artifact> artifacts(const std::string &, const std::string &) override;
  Artifact get_artifact(const std::string &) override;

  void recover() override;
  Run cancel(const std::string &) override;

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
struct LockedAttempt {
  Run run;
  Attempt attempt;
  bool lease_valid{};
  bool launch_valid{};
  bool deadline_valid{};
};
LockedAttempt lock_attempt(pqxx::work &tx, const std::string &id);
LockedAttempt owned(pqxx::work &tx, const std::string &id, int generation,
                    const std::string &instance);
void fail(pqxx::work &, const LockedAttempt &, Failure, const std::string &, std::optional<int>,
          const Timing &);
void require_worker(pqxx::work &tx, const std::string &id, const std::string &session);
} // namespace pg
} // namespace runyard
