#pragma once
#include "runyard/execution/backend.hpp"
#include "runyard/serialization/json.hpp"
#include "runyard/support/http_client.hpp"

namespace runyard {
struct KubernetesConfig {
  std::string api{"https://kubernetes.default.svc"};
  std::string name_space{"runyard"};
  std::string token_file{"/var/run/secrets/kubernetes.io/serviceaccount/token"};
  std::string api_ca{"/var/run/secrets/kubernetes.io/serviceaccount/ca.crt"};
  std::string coordinator{"server:9090"};
  std::string runner_ca_configmap;
  bool development{};
};
Json kubernetes_job(const KubernetesConfig &, const Launch &);
bool kubernetes_job_finished(const Json &job);
class KubernetesBackend final : public ExecutionBackend {
public:
  explicit KubernetesBackend(KubernetesConfig config) : config_(std::move(config)) {}
  std::vector<std::string> inventory() override;
  std::string ensure(const Launch &) override;
  bool has_stopped(const std::string &) override;
  void remove(const std::string &) override;

private:
  HttpResult call(const std::string &, const std::string &, const Json & = Json()) const;
  std::string jobs() const;
  KubernetesConfig config_;
};
} // namespace runyard
