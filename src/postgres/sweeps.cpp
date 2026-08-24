#include "runyard/domain/error.hpp"
#include "runyard/postgres/store.hpp"
#include "runyard/serialization/json.hpp"
#include "runyard/support/crypto.hpp"

namespace runyard {
namespace {
Sweep load_sweep(pqxx::transaction_base &tx, const std::string &id) {
  auto rows = tx.exec("SELECT * FROM sweeps WHERE id=$1", pqxx::params{id});
  if (rows.empty())
    throw Error(ErrorCode::not_found, "sweep not found");
  Sweep result{pg::text(rows[0]["id"]),
               decode_sweep(Json::parse(pg::text(rows[0]["spec"]))),
               {},
               pg::text(rows[0]["created_at"])};
  for (const auto &r :
       tx.exec("SELECT id FROM runs WHERE sweep_id=$1 ORDER BY created_at,id", pqxx::params{id}))
    result.run_ids.push_back(pg::text(r[0]));
  return result;
}
} // namespace
Sweep PostgresStore::submit_sweep(const SweepSpec &spec, const std::vector<RunSpec> &runs,
                                  const std::string &key, const std::string &fingerprint) {
  auto c = pool_.acquire();
  pqxx::work tx(c.get());
  tx.exec("SELECT pg_advisory_xact_lock(hashtextextended($1,1))", pqxx::params{key});
  auto existing = tx.exec("SELECT id,fingerprint FROM sweeps WHERE key=$1", pqxx::params{key});
  if (!existing.empty()) {
    if (pg::text(existing[0][1]) != fingerprint)
      throw Error(ErrorCode::conflict, "sweep key already identifies another request");
    auto result = load_sweep(tx, pg::text(existing[0][0]));
    tx.commit();
    return result;
  }
  auto id = random_id();
  tx.exec("INSERT INTO sweeps(id,spec,key,fingerprint) VALUES($1,$2::jsonb,$3,$4)",
          pqxx::params{id, encode(spec).dump(), key, fingerprint});
  for (const auto &run : runs) {
    validate_submission(run);
    auto run_id = random_id();
    tx.exec("INSERT INTO runs(id,spec,priority,cpu_millis,memory_mib,sweep_id,gpu_count) "
            "VALUES($1,$2::jsonb,$3,$4,$5,$6,$7)",
            pqxx::params{run_id, encode(run).dump(), run.priority, run.resources.cpu_millis,
                         run.resources.memory_mib, id, run.resources.gpu_count});
    pg::event(tx, run_id, "submitted", "sweep " + id);
  }
  auto result = load_sweep(tx, id);
  tx.commit();
  return result;
}
Sweep PostgresStore::get_sweep(const std::string &id) {
  auto c = pool_.acquire();
  pqxx::read_transaction tx(c.get());
  return load_sweep(tx, id);
}
Run PostgresStore::rerun(const std::string &id, const std::string &key) {
  auto c = pool_.acquire();
  pqxx::work tx(c.get());
  tx.exec("SELECT pg_advisory_xact_lock(hashtextextended($1,0))", pqxx::params{key});
  auto fingerprint = sha256("rerun:" + id);
  auto existing =
      tx.exec("SELECT run_id,fingerprint FROM submissions WHERE key=$1", pqxx::params{key});
  if (!existing.empty()) {
    if (pg::text(existing[0][1]) != fingerprint)
      throw Error(ErrorCode::conflict, "key already identifies another request");
    auto result = pg::run(
        tx.exec("SELECT * FROM runs WHERE id=$1", pqxx::params{pg::text(existing[0][0])})[0]);
    tx.commit();
    return result;
  }
  auto source = tx.exec("SELECT * FROM runs WHERE id=$1", pqxx::params{id});
  if (source.empty())
    throw Error(ErrorCode::not_found, "run not found");
  auto old = pg::run(source[0]);
  if (!terminal(old.status))
    throw Error(ErrorCode::conflict, "only terminal runs can be rerun");
  validate_submission(old.spec);
  auto new_id = random_id();
  auto rows =
      tx.exec("INSERT INTO runs(id,spec,priority,cpu_millis,memory_mib,parent_run_id,gpu_count) "
              "VALUES($1,$2::jsonb,$3,$4,$5,$6,$7) RETURNING *",
              pqxx::params{new_id, encode(old.spec).dump(), old.spec.priority,
                           old.spec.resources.cpu_millis, old.spec.resources.memory_mib, id,
                           old.spec.resources.gpu_count});
  tx.exec("INSERT INTO submissions(key,fingerprint,run_id) VALUES($1,$2,$3)",
          pqxx::params{key, fingerprint, new_id});
  pg::event(tx, new_id, "submitted", "rerun of " + id);
  auto result = pg::run(rows[0]);
  tx.commit();
  return result;
}
} // namespace runyard
