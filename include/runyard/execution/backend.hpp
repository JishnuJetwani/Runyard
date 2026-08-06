#pragma once
#include "runyard/domain/model.hpp"

namespace runyard {
struct Launch {
  Assignment assignment;
  std::string capability;
};
class ExecutionBackend {
public:
  virtual ~ExecutionBackend() = default;
  virtual std::vector<std::string> inventory() = 0;
  virtual std::string ensure(const Launch &launch) = 0;
  virtual void remove(const std::string &attempt_id) = 0;
};
} // namespace runyard
