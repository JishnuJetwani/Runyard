#include "runyard/postgres/store.hpp"
namespace runyard {
GpuCapacityTotals PostgresStore::gpu_capacity() {
  auto c = pool_.acquire();
  pqxx::read_transaction tx(c.get());
  auto rows = tx.exec(R"SQL(
    SELECT
      (SELECT count(*) FROM gpu_devices WHERE present AND eligible) AS capacity,
      (SELECT count(*) FROM attempt_gpus WHERE released_at IS NULL) AS reserved,
      (SELECT count(*) FROM gpu_devices d JOIN workers w ON w.id=d.worker_id
       WHERE d.present AND d.eligible AND w.gpu_capable AND w.gpu_ready AND NOT w.drained
         AND w.heartbeat_at>clock_timestamp()-$1*interval '1 second'
         AND w.gpu_observed_at>clock_timestamp()-interval '30 seconds'
         AND NOT EXISTS(SELECT 1 FROM attempt_gpus ag WHERE ag.uuid=d.uuid AND ag.released_at IS NULL)) AS available,
      (SELECT COALESCE(sum(gpu_count),0) FROM runs WHERE status IN ('QUEUED','RETRY_WAIT')) AS pending,
      COALESCE(bool_and(gpu_ready AND COALESCE(gpu_observed_at>clock_timestamp()-interval '30 seconds',false)),true) AS fresh,
      min(gpu_observed_at) AS observed_at,
      floor(EXTRACT(epoch FROM clock_timestamp()-min(gpu_observed_at)))::bigint AS age
    FROM workers WHERE gpu_capable
  )SQL",
                      pqxx::params{timing_.worker_seconds});
  const auto &row = rows[0];
  GpuCapacityTotals result;
  result.capacity = row["capacity"].as<std::int64_t>();
  result.allocatable = result.capacity;
  result.reserved = row["reserved"].as<std::int64_t>();
  result.available = row["available"].as<std::int64_t>();
  result.pending = row["pending"].as<std::int64_t>();
  result.fresh = row["fresh"].as<bool>();
  result.observed_at = pg::text(row["observed_at"]);
  if (!row["age"].is_null())
    result.age_seconds = row["age"].as<std::int64_t>();
  return result;
}
} // namespace runyard
