#pragma once
#include "runyard/domain/model.hpp"

namespace runyard {
class Repository {
public:
  virtual ~Repository() = default;
  virtual void recover() = 0;
  virtual std::optional<Assignment> admit_kubernetes(int max_active) = 0;
  virtual std::vector<Assignment> kubernetes_attempts() = 0;
  virtual void kubernetes_runtime(const std::string &attempt, const std::string &runtime,
                                  bool removed) = 0;
  virtual Sweep submit_sweep(const SweepSpec &, const std::vector<RunSpec> &,
                             const std::string &key, const std::string &fingerprint) = 0;
  virtual Sweep get_sweep(const std::string &id) = 0;
  virtual Run rerun(const std::string &id, const std::string &key) = 0;
  virtual void drain_worker(const std::string &id, bool drained) = 0;
  virtual std::vector<std::string> reconcile(const std::string &worker, const std::string &session,
                                             const std::vector<std::string> &observed) = 0;
  virtual Run cancel(const std::string &id) = 0;
  virtual void verify_owner(const std::string &, int, const std::string &) = 0;
  virtual Artifact publish_artifact(const Artifact &, int, const std::string &) = 0;
  virtual std::vector<Artifact> artifacts(const std::string &run, const std::string &attempt) = 0;
  virtual Artifact get_artifact(const std::string &id) = 0;
  virtual std::int64_t report(const std::string &attempt, int generation,
                              const std::string &instance,
                              const std::vector<Telemetry> &records) = 0;
  virtual std::vector<Telemetry> telemetry(const std::string &run, const std::string &attempt,
                                           std::int64_t after, int limit, const std::string &kind,
                                           const std::string &name) = 0;
  virtual Run submit(const RunSpec &spec, const std::string &key,
                     const std::string &fingerprint) = 0;
  virtual Run get_run(const std::string &id) = 0;
  virtual std::vector<Run> list_runs(int limit, const std::string &after,
                                     const std::string &status) = 0;
  virtual std::vector<Attempt> attempts(const std::string &run_id) = 0;
  virtual std::vector<Event> events(const std::string &run_id, std::int64_t after, int limit) = 0;
  virtual void register_worker(const std::string &id, const std::string &session,
                               Resources capacity) = 0;
  virtual void worker_heartbeat(const std::string &id, const std::string &session) = 0;
  virtual std::vector<Worker> workers() = 0;
  virtual std::optional<Assignment> assign(const std::string &id, const std::string &session) = 0;
  virtual void runtime_report(const std::string &worker, const std::string &session,
                              const std::string &attempt, const std::string &runtime,
                              bool stopped) = 0;
  virtual std::vector<Attempt> cleanup(const std::string &worker, const std::string &session) = 0;
  virtual Assignment start(const std::string &attempt, int generation,
                           const std::string &instance) = 0;
  virtual void heartbeat(const std::string &attempt, int generation,
                         const std::string &instance) = 0;
  virtual void begin_finalization(const std::string &attempt, int generation,
                                  const std::string &instance) = 0;
  virtual void finish(const std::string &attempt, int generation, const std::string &instance,
                      int exit_code, const std::string &reason, std::int64_t final_sequence) = 0;
};
} // namespace runyard
