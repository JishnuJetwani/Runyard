#include "runyard/domain/error.hpp"
#include "runyard/postgres/store.hpp"
#include "runyard/serialization/json.hpp"
#include "runyard/support/crypto.hpp"

namespace runyard {
std::optional<Assignment> PostgresStore::admit_kubernetes(int max_active) {
  if (max_active < 1 || max_active > 100)
    throw Error(ErrorCode::invalid, "Kubernetes admission limit must be 1..100");
  auto c = pool_.acquire();
  pqxx::work tx(c.get());
  // This short transaction serializes the admission count, independently of API latency.
  tx.exec("SELECT pg_advisory_xact_lock(72841903)");
  if (tx.exec("SELECT count(*) FROM attempts WHERE worker_id='@kubernetes' AND "
              "cleanup_status='PENDING'")[0][0]
          .as<int>() >= max_active)
    return std::nullopt;
  auto rows =
      tx.exec("SELECT * FROM runs WHERE status='QUEUED' AND available_at<=clock_timestamp() ORDER "
              "BY priority DESC,created_at,id LIMIT 1 FOR UPDATE SKIP LOCKED");
  if (rows.empty())
    return std::nullopt;
  auto run = pg::run(rows[0]);
  auto id = random_id();
  int generation = run.generation + 1;
  auto attempt =
      tx.exec("INSERT INTO attempts(id,run_id,generation,worker_id,launch_deadline) "
              "VALUES($1,$2,$3,'@kubernetes',clock_timestamp()+$4*interval '1 second') RETURNING *",
              pqxx::params{id, run.id, generation, timing_.launch_seconds});
  tx.exec("UPDATE runs SET status='STARTING',active_attempt=$2,generation=$3 WHERE id=$1",
          pqxx::params{run.id, id, generation});
  pg::event(tx, run.id, "assigned",
            "attempt " + std::to_string(generation) + " admitted to Kubernetes");
  Assignment result{pg::attempt(attempt[0]), run.spec};
  tx.commit();
  return result;
}
std::vector<Assignment> PostgresStore::kubernetes_attempts() {
  auto c = pool_.acquire();
  pqxx::read_transaction tx(c.get());
  std::vector<Assignment> result;
  auto rows =
      tx.exec("SELECT a.*,r.spec FROM attempts a JOIN runs r ON r.id=a.run_id WHERE "
              "a.worker_id='@kubernetes' AND a.cleanup_status='PENDING' ORDER BY a.created_at");
  for (const auto &row : rows)
    result.push_back({pg::attempt(row), decode_spec(Json::parse(pg::text(row["spec"])))});
  return result;
}
void PostgresStore::kubernetes_runtime(const std::string &id, const std::string &runtime,
                                       bool removed) {
  auto c = pool_.acquire();
  pqxx::work tx(c.get());
  auto a = pg::lock_attempt(tx, id);
  if (a.attempt.worker_id != "@kubernetes")
    throw Error(ErrorCode::invalid, "attempt does not belong to Kubernetes");
  if (removed) {
    if (a.attempt.status == "STARTING" || a.attempt.status == "RUNNING" ||
        a.attempt.status == "FINALIZING")
      throw Error(ErrorCode::conflict, "attempt remains active");
    tx.exec("UPDATE attempts SET cleanup_status='DONE' WHERE id=$1", pqxx::params{id});
  } else
    tx.exec("UPDATE attempts SET runtime_id=$2 WHERE id=$1", pqxx::params{id, runtime});
  tx.commit();
}
} // namespace runyard
