#include "runyard/postgres/store.hpp"
#include <algorithm>
#include <cctype>

namespace runyard {
std::map<std::string, double> PostgresStore::statistics() {
  auto c = pool_.acquire();
  pqxx::read_transaction tx(c.get());
  std::map<std::string, double> values;
  for (const auto &state : {"QUEUED", "STARTING", "RUNNING", "FINALIZING", "RETRY_WAIT",
                            "SUCCEEDED", "FAILED", "CANCELLED"}) {
    std::string name = state;
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    values["runs_" + name] = 0;
  }
  for (const auto &r :
       tx.exec("SELECT lower(status) AS status,count(*) AS count FROM runs GROUP BY status"))
    values["runs_" + pg::text(r["status"])] = r["count"].as<double>();
  auto attempts_result = tx.exec(
      "SELECT count(*) AS total,count(*) FILTER(WHERE generation>1) AS retries,count(*) "
      "FILTER(WHERE status='FAILED') AS failures,count(*) FILTER(WHERE cleanup_status='PENDING' "
      "AND status IN ('FAILED','CANCELLED','SUCCEEDED')) AS cleanup FROM attempts");
  auto attempts = attempts_result[0];
  values["attempts"] = attempts["total"].as<double>();
  values["retries"] = attempts["retries"].as<double>();
  values["failures"] = attempts["failures"].as<double>();
  values["cleanup_pending"] = attempts["cleanup"].as<double>();
  auto worker_result =
      tx.exec("SELECT count(*) AS total,COALESCE(sum(cpu_millis),0) AS "
              "cpu,COALESCE(sum(memory_mib),0) AS memory FROM workers WHERE NOT drained "
              "AND heartbeat_at>clock_timestamp()-$1*interval '1 second'",
              pqxx::params{timing_.worker_seconds});
  auto worker = worker_result[0];
  values["workers_available"] = worker["total"].as<double>();
  values["worker_cpu_millis"] = worker["cpu"].as<double>();
  values["worker_memory_mib"] = worker["memory"].as<double>();
  auto reserved_result =
      tx.exec("SELECT COALESCE(sum(r.cpu_millis),0) AS cpu,COALESCE(sum(r.memory_mib),0) AS memory "
              "FROM attempts a JOIN runs r ON r.id=a.run_id WHERE a.cleanup_status='PENDING' AND "
              "a.worker_id<>'@kubernetes'");
  auto reserved = reserved_result[0];
  values["reserved_cpu_millis"] = reserved["cpu"].as<double>();
  values["reserved_memory_mib"] = reserved["memory"].as<double>();
  values["queue_oldest_seconds"] =
      tx.exec("SELECT COALESCE(EXTRACT(epoch FROM clock_timestamp()-min(available_at)),0) FROM "
              "runs WHERE status='QUEUED'")[0][0]
          .as<double>();
  auto gpu_queue =
      tx.exec("SELECT count(*),COALESCE(EXTRACT(epoch FROM clock_timestamp()-min(available_at)),0) "
              "FROM runs WHERE status='QUEUED' AND gpu_count>0");
  values["gpu_queued_runs"] = gpu_queue[0][0].as<double>();
  values["gpu_queue_oldest_seconds"] = gpu_queue[0][1].as<double>();
  for (const auto &[name, expression] : std::map<std::string, std::string>{
           {"queue_delay", "extract(epoch FROM a.created_at-r.created_at)"},
           {"dispatch_delay", "extract(epoch FROM a.started_at-a.created_at)"}}) {
    auto rows =
        tx.exec("SELECT COALESCE(percentile_cont(0.50) WITHIN GROUP(ORDER BY " + expression +
                "),0),COALESCE(percentile_cont(0.95) WITHIN GROUP(ORDER BY " + expression +
                "),0),COALESCE(percentile_cont(0.99) WITHIN GROUP(ORDER BY " + expression +
                "),0) FROM attempts a JOIN runs r ON r.id=a.run_id WHERE a.generation=1 AND "
                "a.created_at>clock_timestamp()-interval '1 hour'");
    values[name + "_p50_seconds"] = rows[0][0].as<double>();
    values[name + "_p95_seconds"] = rows[0][1].as<double>();
    values[name + "_p99_seconds"] = rows[0][2].as<double>();
  }
  return values;
}
} // namespace runyard
