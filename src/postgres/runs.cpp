#include "runyard/domain/error.hpp"
#include "runyard/postgres/store.hpp"
#include "runyard/serialization/json.hpp"
#include "runyard/support/crypto.hpp"

namespace runyard {
namespace pg {
std::string text(const FieldView &f) { return f.is_null() ? "" : f.as<std::string>(); }
Run run(const RowView &r) {
  return {r["id"].as<std::string>(),
          decode_spec(Json::parse(r["spec"].as<std::string>())),
          parse_status(r["status"].as<std::string>()),
          r["generation"].as<int>(),
          text(r["active_attempt"]),
          text(r["sweep_id"]),
          text(r["parent_run_id"]),
          text(r["created_at"]),
          text(r["updated_at"])};
}
Attempt attempt(const RowView &r) {
  Attempt a;
  a.id = text(r["id"]);
  a.run_id = text(r["run_id"]);
  a.generation = r["generation"].as<int>();
  a.worker_id = text(r["worker_id"]);
  a.instance_id = text(r["instance_id"]);
  a.status = text(r["status"]);
  a.reason = text(r["reason"]);
  if (!r["exit_code"].is_null())
    a.exit_code = r["exit_code"].as<int>();
  a.runtime_id = text(r["runtime_id"]);
  a.cleanup_status = text(r["cleanup_status"]);
  a.created_at = text(r["created_at"]);
  a.started_at = text(r["started_at"]);
  a.finished_at = text(r["finished_at"]);
  a.gpu_count = r["gpu_count"].as<int>();
  a.node_name = text(r["node_name"]);
  a.acknowledged_sequence = r["ack_sequence"].as<std::int64_t>();
  return a;
}
void event(pqxx::work &tx, const std::string &id, const std::string &kind,
           const std::string &detail) {
  // Allocate under the run lock: a cursor can never pass an uncommitted earlier event.
  auto sequence =
      tx.exec("UPDATE runs SET event_sequence=event_sequence+1,updated_at=clock_timestamp() WHERE "
              "id=$1 RETURNING event_sequence",
              pqxx::params{id})[0][0]
          .as<std::int64_t>();
  tx.exec("INSERT INTO events(run_id,sequence,kind,detail) VALUES($1,$2,$3,$4)",
          pqxx::params{id, sequence, kind, detail});
}
} // namespace pg
Run PostgresStore::submit(const RunSpec &spec, const std::string &key,
                          const std::string &fingerprint) {
  validate(spec);
  auto connection = pool_.acquire();
  pqxx::work tx(connection.get());
  // Serialize only callers sharing this key, including concurrent first submissions.
  tx.exec("SELECT pg_advisory_xact_lock(hashtextextended($1,0))", pqxx::params{key});
  auto old = tx.exec("SELECT fingerprint,run_id FROM submissions WHERE key=$1", pqxx::params{key});
  if (!old.empty()) {
    if (old[0][0].as<std::string>() != fingerprint)
      throw Error(ErrorCode::conflict, "idempotency key already identifies another request");
    auto result = pg::run(
        tx.exec("SELECT * FROM runs WHERE id=$1", pqxx::params{old[0][1].as<std::string>()})[0]);
    tx.commit();
    return result;
  }
  auto id = random_id();
  auto row = tx.exec("INSERT INTO runs(id,spec,priority,cpu_millis,memory_mib) "
                     "VALUES($1,$2::jsonb,$3,$4,$5) RETURNING *",
                     pqxx::params{id, encode(spec).dump(), spec.priority, spec.resources.cpu_millis,
                                  spec.resources.memory_mib});
  tx.exec("INSERT INTO submissions(key,fingerprint,run_id) VALUES($1,$2,$3)",
          pqxx::params{key, fingerprint, id});
  pg::event(tx, id, "submitted", "run accepted");
  auto result = pg::run(row[0]);
  tx.commit();
  return result;
}
Run PostgresStore::get_run(const std::string &id) {
  auto c = pool_.acquire();
  pqxx::read_transaction tx(c.get());
  auto rows = tx.exec("SELECT * FROM runs WHERE id=$1", pqxx::params{id});
  if (rows.empty())
    throw Error(ErrorCode::not_found, "run not found");
  return pg::run(rows[0]);
}
std::vector<Run> PostgresStore::list_runs(int limit, const std::string &after,
                                          const std::string &status) {
  auto c = pool_.acquire();
  pqxx::read_transaction tx(c.get());
  auto rows = tx.exec(
      "SELECT * FROM runs WHERE ($1='' OR id>$1) AND ($2='' OR status=$2) ORDER BY id LIMIT $3",
      pqxx::params{after, status, std::clamp(limit, 1, 200)});
  std::vector<Run> result;
  for (const auto &r : rows)
    result.push_back(pg::run(r));
  return result;
}
std::vector<Attempt> PostgresStore::attempts(const std::string &id) {
  auto c = pool_.acquire();
  pqxx::read_transaction tx(c.get());
  std::vector<Attempt> result;
  for (const auto &r :
       tx.exec("SELECT * FROM attempts WHERE run_id=$1 ORDER BY generation", pqxx::params{id})) {
    auto attempt = pg::attempt(r);
    pg::load_gpu_allocations(tx, attempt);
    result.push_back(std::move(attempt));
  }
  return result;
}
std::vector<Event> PostgresStore::events(const std::string &id, std::int64_t after, int limit) {
  auto c = pool_.acquire();
  pqxx::read_transaction tx(c.get());
  std::vector<Event> result;
  for (const auto &r :
       tx.exec("SELECT * FROM events WHERE run_id=$1 AND sequence>$2 ORDER BY sequence LIMIT $3",
               pqxx::params{id, after, std::clamp(limit, 1, 1000)}))
    result.push_back({r["sequence"].as<std::int64_t>(), pg::text(r["kind"]), pg::text(r["detail"]),
                      pg::text(r["created_at"])});
  return result;
}
} // namespace runyard
