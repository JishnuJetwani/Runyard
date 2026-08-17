#pragma once
#include "runyard/application/repository.hpp"

namespace runyard {
class RunService {
public:
  explicit RunService(Repository &repository) : repository_(repository) {}
  Run submit(const RunSpec &spec, const std::string &key, const std::string &fingerprint);
  std::vector<Worker> workers(int limit = 100, const std::string &after = "") {
    return repository_.workers(limit, after);
  }
  void drain_worker(const std::string &id, bool drained) { repository_.drain_worker(id, drained); }
  Sweep sweep(const SweepSpec &spec, const std::string &key, const std::string &fingerprint);
  Sweep get_sweep(const std::string &id) { return repository_.get_sweep(id); }
  Run rerun(const std::string &id, const std::string &key);
  Run cancel(const std::string &id) { return repository_.cancel(id); }
  Run get(const std::string &id) { return repository_.get_run(id); }
  std::vector<Run> list(int limit, const std::string &after, const std::string &status);
  std::vector<Attempt> attempts(const std::string &id) {
    repository_.get_run(id);
    return repository_.attempts(id);
  }
  std::vector<Event> events(const std::string &id, std::int64_t after, int limit) {
    repository_.get_run(id);
    return repository_.events(id, after, limit);
  }

  std::vector<Telemetry> telemetry(const std::string &run, const std::string &attempt,
                                   std::int64_t after, int limit, const std::string &kind,
                                   const std::string &name) {
    repository_.get_run(run);
    return repository_.telemetry(run, attempt, after, limit, kind, name);
  }

private:
  Repository &repository_;
};
} // namespace runyard
