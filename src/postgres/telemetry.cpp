#include "runyard/domain/error.hpp"
#include "runyard/postgres/store.hpp"

namespace runyard {
std::int64_t PostgresStore::report(const std::string &id, int generation,
                                   const std::string &instance,
                                   const std::vector<Telemetry> &records) {
  if (records.size() > 256)
    throw Error(ErrorCode::invalid, "telemetry batch exceeds 256 records");
  for (const auto &record : records)
    validate(record);
  auto c = pool_.acquire();
  pqxx::work tx(c.get());
  auto a = pg::owned(tx, id, generation, instance);
  auto sequence = a.attempt.acknowledged_sequence;
  for (const auto &r : records) {
    if (r.sequence <= sequence)
      continue;
    // Return the persisted prefix so a sender can retransmit the missing suffix.
    if (r.sequence != sequence + 1)
      break;
    tx.exec("INSERT INTO telemetry(attempt_id,sequence,kind,text,name,step,value,timestamp_ms) "
            "VALUES($1,$2,$3,$4,$5,$6,$7,$8)",
            pqxx::params{id, r.sequence, r.kind, r.text, r.name, r.step, r.value, r.timestamp_ms});
    sequence = r.sequence;
  }
  tx.exec("UPDATE attempts SET ack_sequence=$2 WHERE id=$1", pqxx::params{id, sequence});
  tx.commit();
  return sequence;
}
std::vector<Telemetry> PostgresStore::telemetry(const std::string &run, const std::string &attempt,
                                                std::int64_t after, int limit,
                                                const std::string &kind, const std::string &name) {
  auto c = pool_.acquire();
  pqxx::read_transaction tx(c.get());
  std::vector<Telemetry> result;
  auto rows = tx.exec(
      R"SQL(SELECT t.* FROM telemetry t JOIN attempts a ON a.id=t.attempt_id JOIN runs r ON r.id=a.run_id
    WHERE r.id=$1 AND a.id=COALESCE(NULLIF($2,''),r.active_attempt) AND t.sequence>$3
    AND (($4='logs' AND t.kind<>'metric') OR ($4='metric' AND t.kind='metric'))
    AND ($5='' OR t.name=$5) ORDER BY t.sequence LIMIT $6)SQL",
      pqxx::params{run, attempt, after, kind, name, std::clamp(limit, 1, 1000)});
  for (const auto &r : rows)
    result.push_back({r["sequence"].as<std::int64_t>(), pg::text(r["kind"]), pg::text(r["text"]),
                      pg::text(r["name"]), r["step"].as<std::int64_t>(), r["value"].as<double>(),
                      r["timestamp_ms"].as<std::int64_t>()});
  return result;
}
} // namespace runyard
