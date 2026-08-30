#pragma once
#include "runyard/domain/capacity.hpp"
#include "runyard/execution/kubernetes.hpp"
#include <functional>
#include <mutex>
#include <stop_token>

namespace runyard {
std::int64_t gpu_quantity(const Json &);
std::int64_t pod_gpu_request(const Json &);
GpuNodeCapacity gpu_node_capacity(const Json &);

class KubernetesCapacity {
public:
  using Fetch = std::function<HttpResult(const std::string &)>;
  KubernetesCapacity(KubernetesConfig, const Clock &, Fetch = {});
  void refresh(std::stop_token = {});
  ClusterGpuSnapshot snapshot() const;

private:
  void each_page(const std::string &, const std::function<void(const Json &)> &, std::stop_token,
                 std::chrono::steady_clock::time_point deadline);
  const Clock &clock_;
  Fetch fetch_;
  mutable std::mutex mutex_;
  ClusterGpuSnapshot last_;
  std::chrono::steady_clock::time_point observed_{};
  bool successful_{};
};
} // namespace runyard
