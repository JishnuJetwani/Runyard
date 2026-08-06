#include "runyard/domain/error.hpp"
#include "runyard/postgres/store.hpp"

namespace runyard {
void pg::fail(pqxx::work &tx, const LockedAttempt &a, Failure failure, const std::string &reason,
              std::optional<int> exit_code, const Timing &timing) {
  bool retry = should_retry(a.run.spec.retry, a.attempt.generation, failure);
  tx.exec("UPDATE attempts SET "
          "status='FAILED',reason=$2,exit_code=$3,finished_at=clock_timestamp() WHERE id=$1",
          pqxx::params{a.attempt.id, reason, exit_code});
  tx.exec(
      "UPDATE runs SET status=$2,available_at=clock_timestamp()+$3*interval '1 second' WHERE id=$1",
      pqxx::params{a.run.id, retry ? "RETRY_WAIT" : "FAILED",
                   retry_delay(a.attempt.generation, timing.retry_base_seconds)});
  event(tx, a.run.id, retry ? "retry_scheduled" : "failed", reason);
}
void PostgresStore::recover() {
  {
    auto c = pool_.acquire();
    pqxx::work tx(c.get());
    auto rows = tx.exec("UPDATE runs SET status='QUEUED' WHERE status='RETRY_WAIT' AND "
                        "available_at<=clock_timestamp() RETURNING id");
    for (const auto &row : rows)
      pg::event(tx, pg::text(row[0]), "requeued", "retry delay elapsed");
    tx.commit();
  }
  std::vector<std::string> candidates;
  {
    auto c = pool_.acquire();
    pqxx::read_transaction tx(c.get());
    auto rows = tx.exec(R"SQL(SELECT id FROM attempts WHERE
      (status='STARTING' AND launch_deadline<=clock_timestamp()) OR
      (status IN ('RUNNING','FINALIZING') AND lease_until<=clock_timestamp()) OR
      (status='RUNNING' AND execution_deadline<=clock_timestamp()) OR
      (status='FINALIZING' AND finalization_deadline<=clock_timestamp())
      ORDER BY created_at LIMIT 100)SQL");
    for (const auto &row : rows)
      candidates.push_back(pg::text(row[0]));
  }
  for (const auto &id : candidates) {
    auto c = pool_.acquire();
    pqxx::work tx(c.get());
    auto a = pg::lock_attempt(tx, id);
    if (a.run.active_attempt != id || terminal(a.run.status) || a.attempt.status == "FAILED") {
      tx.commit();
      continue;
    }
    if (a.run.status == RunStatus::starting && !a.launch_valid)
      pg::fail(tx, a, Failure::infrastructure, "LAUNCH_TIMEOUT", std::nullopt, timing_);
    else if (a.run.status == RunStatus::running && !a.deadline_valid)
      pg::fail(tx, a, Failure::timeout, "TIMEOUT", std::nullopt, timing_);
    else if (a.run.status == RunStatus::finalizing && !a.deadline_valid)
      pg::fail(tx, a, Failure::infrastructure, "FINALIZATION_TIMEOUT", std::nullopt, timing_);
    else if ((a.run.status == RunStatus::running || a.run.status == RunStatus::finalizing) &&
             !a.lease_valid)
      pg::fail(tx, a, Failure::infrastructure, "LEASE_EXPIRED", std::nullopt, timing_);
    tx.commit();
  }
}
} // namespace runyard
