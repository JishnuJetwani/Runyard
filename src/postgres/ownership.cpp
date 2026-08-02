#include "runyard/domain/error.hpp"
#include "runyard/postgres/store.hpp"

namespace runyard {
namespace pg {
LockedAttempt lock_attempt(pqxx::work &tx, const std::string &id) {
  auto lookup = tx.exec("SELECT run_id FROM attempts WHERE id=$1", pqxx::params{id});
  if (lookup.empty())
    throw Error(ErrorCode::not_found, "attempt not found");
  // All attempt mutations take the run lock first, including cancellation/recovery.
  auto r = tx.exec("SELECT * FROM runs WHERE id=$1 FOR UPDATE",
                   pqxx::params{lookup[0][0].as<std::string>()});
  auto a = tx.exec("SELECT *,COALESCE(lease_until>clock_timestamp(),false) AS "
                   "lease_valid,launch_deadline>clock_timestamp() AS launch_valid,CASE WHEN "
                   "status='FINALIZING' THEN finalization_deadline>clock_timestamp() ELSE "
                   "execution_deadline>clock_timestamp() END AS deadline_valid FROM attempts "
                   "WHERE id=$1 FOR UPDATE",
                   pqxx::params{id});
  return {run(r[0]), attempt(a[0]), a[0]["lease_valid"].as<bool>(), a[0]["launch_valid"].as<bool>(),
          a[0]["deadline_valid"].is_null() || a[0]["deadline_valid"].as<bool>()};
}
LockedAttempt owned(pqxx::work &tx, const std::string &id, int generation,
                    const std::string &instance) {
  auto a = lock_attempt(tx, id);
  if (a.run.active_attempt != id || a.run.generation != generation ||
      a.attempt.instance_id != instance || instance.empty() || !a.lease_valid ||
      !a.deadline_valid ||
      (a.run.status != RunStatus::running && a.run.status != RunStatus::finalizing))
    throw Error(ErrorCode::stale, "attempt ownership expired or was replaced");
  return a;
}
} // namespace pg
Assignment PostgresStore::start(const std::string &id, int generation,
                                const std::string &instance) {
  if (instance.empty() || instance.size() > 200)
    throw Error(ErrorCode::invalid, "invalid runner instance identity");
  auto c = pool_.acquire();
  pqxx::work tx(c.get());
  auto a = pg::lock_attempt(tx, id);
  if (a.run.active_attempt != id || a.run.generation != generation || terminal(a.run.status))
    throw Error(ErrorCode::stale, "attempt is no longer current");
  if (a.run.status != RunStatus::starting && a.run.status != RunStatus::running &&
      a.run.status != RunStatus::finalizing)
    throw Error(ErrorCode::stale, "attempt is no longer executing");
  if (!a.attempt.instance_id.empty()) {
    if (a.attempt.instance_id != instance || !a.lease_valid)
      throw Error(ErrorCode::stale, "attempt already claimed or expired");
    tx.commit();
    return {a.attempt, a.run.spec};
  }
  if (a.run.status != RunStatus::starting || !a.launch_valid)
    throw Error(ErrorCode::stale, "launch deadline expired");
  auto row =
      tx.exec("UPDATE attempts SET "
              "instance_id=$2,status='RUNNING',started_at=clock_timestamp(),lease_until=clock_"
              "timestamp()+$3*interval '1 second',execution_deadline=clock_timestamp()+$4*interval "
              "'1 second' WHERE id=$1 RETURNING *",
              pqxx::params{id, instance, timing_.lease_seconds, a.run.spec.timeout_seconds});
  tx.exec("UPDATE runs SET status='RUNNING' WHERE id=$1", pqxx::params{a.run.id});
  pg::event(tx, a.run.id, "started", "runner claimed execution");
  auto result = Assignment{pg::attempt(row[0]), a.run.spec};
  tx.commit();
  return result;
}
void PostgresStore::heartbeat(const std::string &id, int generation, const std::string &instance) {
  auto c = pool_.acquire();
  pqxx::work tx(c.get());
  pg::owned(tx, id, generation, instance);
  tx.exec("UPDATE attempts SET lease_until=clock_timestamp()+$2*interval '1 second' WHERE id=$1",
          pqxx::params{id, timing_.lease_seconds});
  tx.commit();
}
void PostgresStore::begin_finalization(const std::string &id, int generation,
                                       const std::string &instance) {
  auto c = pool_.acquire();
  pqxx::work tx(c.get());
  auto a = pg::owned(tx, id, generation, instance);
  if (a.run.status == RunStatus::running) {
    tx.exec("UPDATE attempts SET "
            "status='FINALIZING',finalization_deadline=clock_timestamp()+$2*interval '1 second' "
            "WHERE id=$1",
            pqxx::params{id, timing_.finalization_seconds});
    tx.exec("UPDATE runs SET status='FINALIZING' WHERE id=$1", pqxx::params{a.run.id});
    pg::event(tx, a.run.id, "finalizing", "collecting execution outputs");
  }
  tx.commit();
}
void PostgresStore::finish(const std::string &id, int generation, const std::string &instance,
                           int exit_code, const std::string &reason, std::int64_t final_sequence) {
  auto c = pool_.acquire();
  pqxx::work tx(c.get());
  auto a = pg::lock_attempt(tx, id);
  if (a.run.active_attempt == id && a.attempt.instance_id == instance &&
      a.attempt.generation == generation && a.attempt.exit_code == exit_code &&
      (a.attempt.status == "SUCCEEDED" || a.attempt.status == "FAILED")) {
    tx.commit();
    return;
  }
  a = pg::owned(tx, id, generation, instance);
  if (a.attempt.acknowledged_sequence != final_sequence)
    throw Error(ErrorCode::conflict, "telemetry must be acknowledged before completion");
  if (exit_code == 0 && a.run.status != RunStatus::finalizing)
    throw Error(ErrorCode::conflict, "successful completion requires finalization");
  if (exit_code == 0) {
    tx.exec("UPDATE attempts SET "
            "status='SUCCEEDED',exit_code=0,reason=$2,finished_at=clock_timestamp() WHERE id=$1",
            pqxx::params{id, reason});
    tx.exec("UPDATE runs SET status='SUCCEEDED' WHERE id=$1", pqxx::params{a.run.id});
    pg::event(tx, a.run.id, "completed", "SUCCEEDED");
  } else {
    auto failure = reason == "TIMEOUT" ? Failure::timeout
                   : (reason == "STOP_REQUESTED" || reason == "LEASE_LOST")
                       ? Failure::infrastructure
                       : Failure::exit_error;
    pg::fail(tx, a, failure, reason.empty() ? "EXIT_ERROR" : reason, exit_code, timing_);
  }
  tx.commit();
}
} // namespace runyard
