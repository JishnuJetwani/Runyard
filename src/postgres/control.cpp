#include "runyard/domain/error.hpp"
#include "runyard/postgres/store.hpp"

namespace runyard {
Run PostgresStore::cancel(const std::string &id) {
  auto c = pool_.acquire();
  pqxx::work tx(c.get());
  auto rows = tx.exec("SELECT * FROM runs WHERE id=$1 FOR UPDATE", pqxx::params{id});
  if (rows.empty())
    throw Error(ErrorCode::not_found, "run not found");
  auto run = pg::run(rows[0]);
  if (terminal(run.status)) {
    tx.commit();
    return run;
  }
  tx.exec("UPDATE attempts SET "
          "status='CANCELLED',reason='USER_CANCELLED',lease_until=clock_timestamp(),finished_at="
          "clock_timestamp() WHERE run_id=$1 AND status IN ('STARTING','RUNNING','FINALIZING')",
          pqxx::params{id});
  auto updated =
      tx.exec("UPDATE runs SET status='CANCELLED' WHERE id=$1 RETURNING *", pqxx::params{id});
  pg::event(tx, id, "cancelled", "results fenced; remote cleanup may still be pending");
  auto result = pg::run(updated[0]);
  tx.commit();
  return result;
}
} // namespace runyard
