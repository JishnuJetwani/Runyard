#include "runyard/domain/error.hpp"
#include "runyard/postgres/store.hpp"
#include "runyard/support/crypto.hpp"

namespace runyard {
void pg::require_worker(pqxx::work &tx, const std::string &id, const std::string &session) {
  auto rows = tx.exec("SELECT session FROM workers WHERE id=$1 FOR UPDATE", pqxx::params{id});
  if (rows.empty() || rows[0][0].as<std::string>() != session)
    throw Error(ErrorCode::stale, "worker session is no longer current");
}
void PostgresStore::register_worker(const std::string &id, const std::string &session,
                                    Resources capacity) {
  if (id.empty() || id.starts_with("@") || id.size() > 100 || session.empty() ||
      capacity.cpu_millis <= 0 || capacity.memory_mib <= 0)
    throw Error(ErrorCode::invalid, "invalid worker registration");
  auto c = pool_.acquire();
  pqxx::work tx(c.get());
  tx.exec(
      "INSERT INTO workers(id,session,cpu_millis,memory_mib) VALUES($1,$2,$3,$4) ON CONFLICT(id) "
      "DO UPDATE SET session=$2,cpu_millis=$3,memory_mib=$4,heartbeat_at=clock_timestamp()",
      pqxx::params{id, session, capacity.cpu_millis, capacity.memory_mib});
  tx.commit();
}
void PostgresStore::worker_heartbeat(const std::string &id, const std::string &session) {
  auto c = pool_.acquire();
  pqxx::work tx(c.get());
  pg::require_worker(tx, id, session);
  tx.exec("UPDATE workers SET heartbeat_at=clock_timestamp() WHERE id=$1", pqxx::params{id});
  tx.commit();
}
std::vector<Worker> PostgresStore::workers(int limit, const std::string &after) {
  auto c = pool_.acquire();
  pqxx::read_transaction tx(c.get());
  std::vector<Worker> result;
  auto rows =
      tx.exec(R"SQL(SELECT w.*,w.heartbeat_at>clock_timestamp()-$1*interval '1 second' AS available,
    COALESCE((SELECT sum(r.cpu_millis) FROM attempts a JOIN runs r ON a.run_id=r.id WHERE a.worker_id=w.id AND a.cleanup_status='PENDING'),0) AS reserved_cpu,
    COALESCE((SELECT sum(r.memory_mib) FROM attempts a JOIN runs r ON a.run_id=r.id WHERE a.worker_id=w.id AND a.cleanup_status='PENDING'),0) AS reserved_memory
    FROM workers w WHERE w.id>$2 ORDER BY w.id LIMIT $3)SQL",
              pqxx::params{timing_.worker_seconds, after, std::clamp(limit, 1, 200)});
  for (const auto &r : rows)
    result.push_back({pg::text(r["id"]),
                      pg::text(r["session"]),
                      {r["cpu_millis"].as<int>(), r["memory_mib"].as<int>()},
                      {r["reserved_cpu"].as<int>(), r["reserved_memory"].as<int>()},
                      r["drained"].as<bool>(),
                      r["available"].as<bool>(),
                      pg::text(r["heartbeat_at"])});
  return result;
}
std::optional<Assignment> PostgresStore::assign(const std::string &id, const std::string &session) {
  auto c = pool_.acquire();
  pqxx::work tx(c.get());
  pg::require_worker(tx, id, session);
  auto worker = tx.exec("SELECT *,heartbeat_at>clock_timestamp()-$2*interval '1 second' AS "
                        "available FROM workers WHERE id=$1",
                        pqxx::params{id, timing_.worker_seconds});
  if (!worker[0]["available"].as<bool>())
    return std::nullopt;
  // Replay an unacknowledged launch before reserving any more capacity.
  auto pending = tx.exec("SELECT a.* FROM attempts a JOIN runs r ON r.active_attempt=a.id WHERE "
                         "a.worker_id=$1 AND a.status='STARTING' AND a.runtime_id IS NULL AND "
                         "a.launch_deadline>clock_timestamp() ORDER BY a.created_at LIMIT 1",
                         pqxx::params{id});
  if (!pending.empty()) {
    auto a = pg::attempt(pending[0]);
    auto r = pg::run(tx.exec("SELECT * FROM runs WHERE id=$1", pqxx::params{a.run_id})[0]);
    tx.commit();
    return Assignment{a, r.spec};
  }
  if (worker[0]["drained"].as<bool>())
    return std::nullopt;
  auto reserved =
      tx.exec("SELECT COALESCE(sum(r.cpu_millis),0),COALESCE(sum(r.memory_mib),0) FROM attempts a "
              "JOIN runs r ON r.id=a.run_id WHERE a.worker_id=$1 AND a.cleanup_status='PENDING'",
              pqxx::params{id});
  int cpu = worker[0]["cpu_millis"].as<int>() - reserved[0][0].as<int>();
  int memory = worker[0]["memory_mib"].as<int>() - reserved[0][1].as<int>();
  auto candidates = tx.exec("SELECT * FROM runs WHERE status='QUEUED' AND "
                            "available_at<=clock_timestamp() AND cpu_millis<=$1 AND memory_mib<=$2 "
                            "ORDER BY priority DESC,created_at,id LIMIT 1 FOR UPDATE SKIP LOCKED",
                            pqxx::params{cpu, memory});
  if (candidates.empty())
    return std::nullopt;
  auto run = pg::run(candidates[0]);
  auto attempt_id = random_id();
  int generation = run.generation + 1;
  auto attempts =
      tx.exec("INSERT INTO attempts(id,run_id,generation,worker_id,launch_deadline) "
              "VALUES($1,$2,$3,$4,clock_timestamp()+$5*interval '1 second') RETURNING *",
              pqxx::params{attempt_id, run.id, generation, id, timing_.launch_seconds});
  tx.exec("UPDATE runs SET status='STARTING',active_attempt=$2,generation=$3 WHERE id=$1",
          pqxx::params{run.id, attempt_id, generation});
  pg::event(tx, run.id, "assigned", "attempt " + std::to_string(generation) + " assigned to " + id);
  auto result = Assignment{pg::attempt(attempts[0]), run.spec};
  tx.commit();
  return result;
}
void PostgresStore::runtime_report(const std::string &worker, const std::string &session,
                                   const std::string &id, const std::string &runtime,
                                   bool stopped) {
  auto c = pool_.acquire();
  pqxx::work tx(c.get());
  pg::require_worker(tx, worker, session);
  auto a = pg::lock_attempt(tx, id);
  if (a.attempt.worker_id != worker)
    throw Error(ErrorCode::unauthorized, "attempt belongs to another worker");
  if (stopped) {
    if (a.attempt.status == "STARTING" || a.attempt.status == "RUNNING" ||
        a.attempt.status == "FINALIZING")
      throw Error(ErrorCode::conflict, "attempt is still active");
    tx.exec("UPDATE attempts SET cleanup_status='DONE' WHERE id=$1", pqxx::params{id});
  } else
    tx.exec("UPDATE attempts SET runtime_id=$2 WHERE id=$1", pqxx::params{id, runtime});
  tx.commit();
}
std::vector<Attempt> PostgresStore::cleanup(const std::string &worker, const std::string &session) {
  auto c = pool_.acquire();
  pqxx::work tx(c.get());
  pg::require_worker(tx, worker, session);
  std::vector<Attempt> result;
  for (const auto &r :
       tx.exec("SELECT * FROM attempts WHERE worker_id=$1 AND cleanup_status='PENDING' AND status "
               "NOT IN ('STARTING','RUNNING','FINALIZING')",
               pqxx::params{worker}))
    result.push_back(pg::attempt(r));
  tx.commit();
  return result;
}
} // namespace runyard

namespace runyard {
void PostgresStore::drain_worker(const std::string &id, bool drained) {
  auto c = pool_.acquire();
  pqxx::work tx(c.get());
  auto rows =
      tx.exec("UPDATE workers SET drained=$2 WHERE id=$1 RETURNING id", pqxx::params{id, drained});
  if (rows.empty())
    throw Error(ErrorCode::not_found, "worker not found");
  tx.commit();
}
std::vector<std::string> PostgresStore::reconcile(const std::string &worker,
                                                  const std::string &session,
                                                  const std::vector<std::string> &observed) {
  if (observed.size() > 1000)
    throw Error(ErrorCode::invalid, "inventory exceeds 1000 containers");
  auto c = pool_.acquire();
  pqxx::work tx(c.get());
  pg::require_worker(tx, worker, session);
  std::vector<std::string> stop;
  for (const auto &id : observed) {
    auto rows = tx.exec("SELECT a.worker_id,a.status,r.active_attempt FROM attempts a JOIN runs r "
                        "ON r.id=a.run_id WHERE a.id=$1",
                        pqxx::params{id});
    if (rows.empty()) {
      stop.push_back(id);
      continue;
    }
    if (pg::text(rows[0]["worker_id"]) != worker)
      throw Error(ErrorCode::unauthorized, "inventory contains another worker's attempt");
    auto status = pg::text(rows[0]["status"]);
    if (pg::text(rows[0]["active_attempt"]) != id ||
        (status != "STARTING" && status != "RUNNING" && status != "FINALIZING"))
      stop.push_back(id);
  }
  tx.commit();
  return stop;
}
} // namespace runyard
