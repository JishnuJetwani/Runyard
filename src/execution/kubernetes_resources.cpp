#include "runyard/domain/error.hpp"
#include "runyard/execution/kubernetes_capacity.hpp"
#include <cmath>
#include <regex>

namespace runyard {
std::int64_t gpu_quantity(const Json &quantity) {
  if (quantity.is_number_integer()) {
    auto value = quantity.get<std::int64_t>();
    if (value >= 0 && value <= 1000000000000LL)
      return value;
  } else if (quantity.is_string()) {
    static const std::regex pattern(R"(^([0-9]+(?:\.[0-9]+)?)([eE][+-]?[0-9]+|[numkKMGTPE]i?)?$)");
    auto text = quantity.get<std::string>();
    std::smatch match;
    if (text.size() <= 64 && std::regex_match(text, match, pattern)) {
      long double scale = 1;
      auto suffix = match[2].str();
      if (suffix.size() > 1 && (suffix.front() == 'e' || suffix.front() == 'E') &&
          suffix.back() != 'i')
        scale = std::pow(10.L, std::stold(suffix.substr(1)));
      else if (!suffix.empty()) {
        const std::map<char, int> powers{{'n', -9}, {'u', -6}, {'m', -3}, {'k', 3},  {'K', 3},
                                         {'M', 6},  {'G', 9},  {'T', 12}, {'P', 15}, {'E', 18}};
        auto power = powers.at(suffix.front());
        scale = std::pow(suffix.back() == 'i' ? 1024.L : 10.L,
                         suffix.back() == 'i' ? power / 3 : power);
      }
      auto value = std::stold(match[1].str()) * scale;
      if (std::isfinite(value) && value >= 0 && value <= 1000000000000.L &&
          std::abs(value - std::round(value)) < 1e-9L)
        return static_cast<std::int64_t>(std::round(value));
    }
  }
  throw Error(ErrorCode::invalid, "invalid whole-GPU resource quantity");
}
namespace {
std::int64_t requested(const Json &container) {
  auto resources = container.value("resources", Json::object());
  auto requests = resources.value("requests", Json::object());
  auto limits = resources.value("limits", Json::object());
  return gpu_quantity(requests.value("nvidia.com/gpu", limits.value("nvidia.com/gpu", Json(0))));
}
} // namespace
std::int64_t pod_gpu_request(const Json &pod) {
  const auto &spec = pod.at("spec");
  std::int64_t application = 0, sidecars = 0, initialization = 0;
  for (const auto &container : spec.value("containers", Json::array()))
    application += requested(container);
  for (const auto &container : spec.value("initContainers", Json::array())) {
    auto count = requested(container);
    if (container.value("restartPolicy", "") == "Always") {
      sidecars += count;
      initialization = std::max(initialization, sidecars);
    } else {
      // Previously started restartable init containers coexist with each subsequent init.
      initialization = std::max(initialization, sidecars + count);
    }
  }
  auto overhead =
      gpu_quantity(spec.value("overhead", Json::object()).value("nvidia.com/gpu", Json(0)));
  return std::max(application + sidecars, initialization) + overhead;
}
GpuNodeCapacity gpu_node_capacity(const Json &node) {
  GpuNodeCapacity result;
  result.name = node.at("metadata").at("name").get<std::string>();
  auto status = node.value("status", Json::object());
  result.capacity =
      gpu_quantity(status.value("capacity", Json::object()).value("nvidia.com/gpu", Json(0)));
  result.allocatable =
      gpu_quantity(status.value("allocatable", Json::object()).value("nvidia.com/gpu", Json(0)));
  for (const auto &condition : status.value("conditions", Json::array()))
    if (condition.value("type", "") == "Ready")
      result.ready = condition.value("status", "") == "True";
  result.schedulable = !node.value("spec", Json::object()).value("unschedulable", false);
  result.eligible =
      result.ready && result.schedulable &&
      node.at("metadata").value("labels", Json::object()).value("runyard.io/gpu-mode", "") ==
          "exclusive";
  return result;
}
} // namespace runyard
