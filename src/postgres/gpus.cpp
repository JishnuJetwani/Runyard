#include "runyard/domain/error.hpp"
#include "runyard/postgres/store.hpp"
#include <limits>
#include <set>

namespace runyard {
void PostgresStore::register_worker(const std::string &id, const std::string &session,
                                    Resources capacity, GpuRegistration gpu) {
  if (id.empty() || id.starts_with("@") || id.size() > 100 || session.empty() ||
      session.size() > 200 || capacity.cpu_millis <= 0 || capacity.memory_mib <= 0 ||
      gpu.engine_id.size() > 200 || (gpu.capable && gpu.engine_id.empty()))
    throw Error(ErrorCode::invalid, "invalid worker registration");
  auto c = pool_.acquire();
  pqxx::work tx(c.get());
  tx.exec("SELECT pg_advisory_xact_lock(hashtextextended($1,2))", pqxx::params{id});
  auto old = tx.exec("SELECT engine_id FROM workers WHERE id=$1 FOR UPDATE", pqxx::params{id});
  if (!old.empty() && !pg::text(old[0][0]).empty() && pg::text(old[0][0]) != gpu.engine_id &&
      !tx.exec("SELECT id FROM attempts WHERE worker_id=$1 AND cleanup_status='PENDING' LIMIT 1",
               pqxx::params{id})
           .empty())
    throw Error(ErrorCode::conflict, "worker engine changed with unresolved attempts");
  tx.exec("INSERT INTO workers(id,session,cpu_millis,memory_mib,engine_id,gpu_capable) "
          "VALUES($1,$2,$3,$4,$5,$6) ON CONFLICT(id) DO UPDATE SET "
          "session=$2,cpu_millis=$3,memory_mib=$4,engine_id=$5,gpu_capable=$6,gpu_ready=false,"
          "gpu_sequence=0,gpu_observed_at=NULL,heartbeat_at=clock_timestamp()",
          pqxx::params{id, session, capacity.cpu_millis, capacity.memory_mib, gpu.engine_id,
                       gpu.capable});
  tx.commit();
}

void PostgresStore::report_gpu_inventory(const std::string &worker, const std::string &session,
                                         const GpuSnapshot &snapshot) {
  if (snapshot.sequence <= 0 || snapshot.devices.size() > 64)
    throw Error(ErrorCode::invalid, "invalid GPU inventory size or sequence");
  std::set<std::string> unique;
  for (const auto &device : snapshot.devices) {
    if (!device.uuid.starts_with("GPU-") || device.uuid.size() > 100 ||
        device.uuid.find('\0') != std::string::npos || device.name.size() > 200 ||
        device.reason.size() > 500 || device.memory_mib > 1048576 ||
        !unique.insert(device.uuid).second)
      throw Error(ErrorCode::invalid, "invalid or duplicate GPU descriptor");
  }
  auto c = pool_.acquire();
  pqxx::work tx(c.get());
  pg::require_worker(tx, worker, session);
  auto workers =
      tx.exec("SELECT gpu_capable,gpu_sequence FROM workers WHERE id=$1", pqxx::params{worker});
  auto row = workers[0];
  if (!row[0].as<bool>())
    throw Error(ErrorCode::conflict, "worker has not enabled GPU execution");
  if (snapshot.sequence <= row[1].as<std::int64_t>())
    return;
  if (snapshot.ready) {
    // Failed discovery changes readiness only; it cannot erase inventory or allocations.
    tx.exec("UPDATE gpu_devices SET present=false WHERE worker_id=$1", pqxx::params{worker});
    for (const auto &uuid : unique) {
      const auto &device = *std::find_if(snapshot.devices.begin(), snapshot.devices.end(),
                                         [&](const auto &d) { return d.uuid == uuid; });
      auto rows =
          tx.exec("INSERT INTO gpu_devices(uuid,worker_id,name,memory_mib,eligible,reason) "
                  "VALUES($1,$2,$3,$4,$5,$6) ON CONFLICT(uuid) DO UPDATE SET name=$3,memory_mib=$4,"
                  "eligible=$5,reason=$6,present=true,observed_at=clock_timestamp() "
                  "WHERE gpu_devices.worker_id=$2 RETURNING uuid",
                  pqxx::params{device.uuid, worker, device.name, device.memory_mib, device.eligible,
                               device.reason});
      if (rows.empty())
        throw Error(ErrorCode::conflict, "GPU UUID belongs to another worker");
    }
  }
  tx.exec(
      "UPDATE workers SET gpu_sequence=$2,gpu_ready=$3,"
      "gpu_observed_at=CASE WHEN $3 THEN clock_timestamp() ELSE gpu_observed_at END WHERE id=$1",
      pqxx::params{worker, snapshot.sequence, snapshot.ready});
  tx.commit();
}

void pg::load_gpu_allocations(pqxx::transaction_base &tx, Attempt &attempt) {
  for (const auto &row : tx.exec("SELECT * FROM attempt_gpus WHERE attempt_id=$1 ORDER BY uuid",
                                 pqxx::params{attempt.id})) {
    attempt.gpu_allocations.push_back(
        {{text(row["uuid"]), text(row["name"]), row["memory_mib"].as<std::uint64_t>(), true, ""},
         text(row["allocated_at"]),
         text(row["released_at"])});
  }
}
} // namespace runyard
