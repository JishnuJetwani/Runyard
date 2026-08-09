#include "runyard/application/kubernetes.hpp"
#include "runyard/support/crypto.hpp"
#include <set>
#include <spdlog/spdlog.h>

namespace runyard {
void KubernetesController::launch(const Assignment &a) {
  auto capability =
      sign(signing_key_, "attempt:" + a.attempt.id + ":" + std::to_string(a.attempt.generation));
  auto runtime = backend_.ensure({a, capability});
  repository_.kubernetes_runtime(a.attempt.id, runtime, false);
}
void KubernetesController::tick() {
  auto attempts = repository_.kubernetes_attempts();
  std::set<std::string> known;
  for (const auto &a : attempts)
    known.insert(a.attempt.id);
  for (const auto &id : backend_.inventory())
    if (!known.contains(id))
      backend_.remove(id);
  for (const auto &a : attempts) {
    try {
      if (a.attempt.status != "STARTING" && a.attempt.status != "RUNNING" &&
          a.attempt.status != "FINALIZING") {
        backend_.remove(a.attempt.id);
        repository_.kubernetes_runtime(a.attempt.id, "", true);
      } else if (a.attempt.runtime_id.empty())
        launch(a);
    } catch (const std::exception &e) {
      spdlog::warn("Kubernetes attempt {}: {}", a.attempt.id, e.what());
    }
  }
  for (int i = 0; i < 4; ++i) {
    auto assignment = repository_.admit_kubernetes(max_active_);
    if (!assignment)
      break;
    launch(*assignment);
  }
}
} // namespace runyard
