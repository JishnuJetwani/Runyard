#pragma once
#include "runyard/domain/model.hpp"

namespace runyard {
class Repository {
public:
  virtual ~Repository() = default;
  virtual Run submit(const RunSpec &spec, const std::string &key,
                     const std::string &fingerprint) = 0;
  virtual Run get_run(const std::string &id) = 0;
  virtual std::vector<Run> list_runs(int limit, const std::string &after,
                                     const std::string &status) = 0;
  virtual std::vector<Attempt> attempts(const std::string &run_id) = 0;
  virtual std::vector<Event> events(const std::string &run_id, std::int64_t after, int limit) = 0;
};
} // namespace runyard
