#include "runyard/application/runs.hpp"
#include "runyard/domain/error.hpp"

namespace runyard {
Run RunService::submit(const RunSpec &spec, const std::string &key,
                       const std::string &fingerprint) {
  validate_submission(spec);
  if (key.empty() || key.size() > 200)
    throw Error(ErrorCode::invalid, "Idempotency-Key must contain 1 to 200 characters");
  return repository_.submit(spec, key, fingerprint);
}
std::vector<Run> RunService::list(int limit, const std::string &after, const std::string &status) {
  if (limit < 1 || limit > 200)
    throw Error(ErrorCode::invalid, "limit must be between 1 and 200");
  if (!status.empty())
    parse_status(status);
  return repository_.list_runs(limit, after, status);
}
} // namespace runyard

namespace runyard {
Sweep RunService::sweep(const SweepSpec &spec, const std::string &key,
                        const std::string &fingerprint) {
  if (key.empty() || key.size() > 200)
    throw Error(ErrorCode::invalid, "Idempotency-Key must contain 1 to 200 characters");
  auto runs = expand_sweep(spec.base, spec.grid);
  return repository_.submit_sweep(spec, runs, key, fingerprint);
}
Run RunService::rerun(const std::string &id, const std::string &key) {
  if (key.empty() || key.size() > 200)
    throw Error(ErrorCode::invalid, "Idempotency-Key must contain 1 to 200 characters");
  return repository_.rerun(id, key);
}
} // namespace runyard
