#include "runyard/execution/kubernetes_capacity.hpp"
#include "runyard/domain/error.hpp"
#include <ctime>
#include <set>

namespace runyard {
namespace {
std::string timestamp() {
  auto now = std::time(nullptr);
  std::tm utc{};
  gmtime_r(&now, &utc);
  char text[32]{};
  std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return text;
}
} // namespace
KubernetesCapacity::KubernetesCapacity(KubernetesConfig config, const Clock &clock, Fetch fetch)
    : clock_(clock), fetch_(std::move(fetch)) {
  if (!fetch_)
    fetch_ = [config = std::move(config)](const std::string &path) {
      return kubernetes_request(config, "GET", path);
    };
}
void KubernetesCapacity::each_page(const std::string &resource,
                                   const std::function<void(const Json &)> &consume,
                                   std::stop_token stop,
                                   std::chrono::steady_clock::time_point deadline) {
  std::set<std::string> cursors;
  std::string cursor;
  std::size_t total = 0;
  do {
    if (stop.stop_requested() || clock_.now() >= deadline)
      throw Error(ErrorCode::unavailable, "capacity scan interrupted or timed out");
    auto response = fetch_(resource + "?limit=100&continue=" + HttpClient::escape(cursor));
    if (response.status != 200)
      throw Error(ErrorCode::unavailable, "cluster capacity listing unavailable");
    auto page = Json::parse(response.body);
    for (const auto &item : page.at("items")) {
      if (++total > 10000)
        throw Error(ErrorCode::exhausted, "cluster capacity inventory exceeds 10000 resources");
      consume(item);
    }
    cursor = page.at("metadata").value("continue", "");
    if (!cursor.empty() && !cursors.insert(cursor).second)
      throw Error(ErrorCode::unavailable, "cluster capacity pagination repeated a cursor");
  } while (!cursor.empty());
}
void KubernetesCapacity::refresh(std::stop_token stop) {
  try {
    ClusterGpuSnapshot next;
    std::map<std::string, std::int64_t> reservations;
    auto deadline = clock_.now() + std::chrono::seconds(10);
    each_page(
        "/api/v1/nodes", [&](const Json &node) { next.nodes.push_back(gpu_node_capacity(node)); },
        stop, deadline);
    each_page(
        "/api/v1/pods",
        [&](const Json &pod) {
          auto phase = pod.value("status", Json::object()).value("phase", "");
          if (phase == "Succeeded" || phase == "Failed")
            return;
          auto count = pod_gpu_request(pod);
          auto node = pod.at("spec").value("nodeName", "");
          if (node.empty())
            next.pending += count;
          else
            reservations[node] += count;
        },
        stop, deadline);
    for (auto &node : next.nodes)
      node.reserved = reservations[node.name];
    std::sort(next.nodes.begin(), next.nodes.end(),
              [](const auto &a, const auto &b) { return a.name < b.name; });
    next.observed_at = timestamp();
    std::lock_guard lock(mutex_);
    last_ = std::move(next);
    observed_ = clock_.now();
    successful_ = true;
  } catch (...) {
    std::lock_guard lock(mutex_);
    successful_ = false;
    throw;
  }
}
ClusterGpuSnapshot KubernetesCapacity::snapshot() const {
  std::lock_guard lock(mutex_);
  auto result = last_;
  if (!result.observed_at.empty()) {
    result.age_seconds = std::max<std::int64_t>(
        0, std::chrono::duration_cast<std::chrono::seconds>(clock_.now() - observed_).count());
    result.fresh = successful_ && *result.age_seconds < 45;
  }
  return result;
}
} // namespace runyard
