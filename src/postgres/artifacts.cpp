#include "runyard/domain/error.hpp"
#include "runyard/postgres/store.hpp"

namespace runyard {
namespace {
Artifact artifact(const pg::RowView &r) {
  return {pg::text(r["id"]),          pg::text(r["attempt_id"]), pg::text(r["path"]),
          pg::text(r["storage_key"]), pg::text(r["sha256"]),     r["size"].as<std::uint64_t>()};
}
} // namespace
void PostgresStore::verify_owner(const std::string &id, int generation,
                                 const std::string &instance) {
  auto c = pool_.acquire();
  pqxx::work tx(c.get());
  pg::owned(tx, id, generation, instance);
  tx.commit();
}
Artifact PostgresStore::publish_artifact(const Artifact &value, int generation,
                                         const std::string &instance) {
  auto c = pool_.acquire();
  pqxx::work tx(c.get());
  auto a = pg::owned(tx, value.attempt_id, generation, instance);
  if (a.run.status != RunStatus::finalizing)
    throw Error(ErrorCode::conflict, "artifacts require finalization");
  auto existing = tx.exec("SELECT * FROM artifacts WHERE attempt_id=$1 AND path=$2",
                          pqxx::params{value.attempt_id, value.path});
  if (!existing.empty()) {
    auto old = artifact(existing[0]);
    if (old.sha256 != value.sha256 || old.size != value.size)
      throw Error(ErrorCode::conflict, "artifact path already contains different data");
    tx.commit();
    return old;
  }
  auto total = tx.exec("SELECT COALESCE(sum(size),0),count(*) FROM artifacts WHERE attempt_id=$1",
                       pqxx::params{value.attempt_id});
  if (total[0][0].as<std::uint64_t>() + value.size > 1024ULL * 1024 * 1024 ||
      total[0][1].as<int>() >= 1000)
    throw Error(ErrorCode::exhausted, "attempt artifact quota exceeded");
  tx.exec(
      "INSERT INTO artifacts(id,attempt_id,path,storage_key,sha256,size) VALUES($1,$2,$3,$4,$5,$6)",
      pqxx::params{value.id, value.attempt_id, value.path, value.storage_key, value.sha256,
                   value.size});
  tx.commit();
  return value;
}
std::vector<Artifact> PostgresStore::artifacts(const std::string &run, const std::string &attempt) {
  auto c = pool_.acquire();
  pqxx::read_transaction tx(c.get());
  std::vector<Artifact> result;
  for (const auto &r : tx.exec("SELECT f.* FROM artifacts f JOIN attempts a ON a.id=f.attempt_id "
                               "JOIN runs r ON r.id=a.run_id WHERE r.id=$1 AND "
                               "a.id=COALESCE(NULLIF($2,''),r.active_attempt) ORDER BY f.path",
                               pqxx::params{run, attempt}))
    result.push_back(artifact(r));
  return result;
}
Artifact PostgresStore::get_artifact(const std::string &id) {
  auto c = pool_.acquire();
  pqxx::read_transaction tx(c.get());
  auto rows = tx.exec("SELECT * FROM artifacts WHERE id=$1", pqxx::params{id});
  if (rows.empty())
    throw Error(ErrorCode::not_found, "artifact not found");
  return artifact(rows[0]);
}
} // namespace runyard
