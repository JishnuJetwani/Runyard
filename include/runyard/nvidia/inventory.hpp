#pragma once
#include "runyard/domain/model.hpp"
#include <set>

namespace runyard {
class GpuInventory {
public:
  virtual ~GpuInventory() = default;
  virtual std::vector<GpuDevice> discover() = 0;
};
class NvidiaInventory final : public GpuInventory {
public:
  explicit NvidiaInventory(std::set<std::string> allowed = {},
                           std::string library = "libnvidia-ml.so.1")
      : allowed_(std::move(allowed)), library_(std::move(library)) {}
  std::vector<GpuDevice> discover() override;

private:
  std::set<std::string> allowed_;
  std::string library_;
};
std::set<std::string> gpu_allowlist(const std::string &value);
} // namespace runyard
