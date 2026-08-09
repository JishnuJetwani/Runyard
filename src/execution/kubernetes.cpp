#include "runyard/execution/kubernetes.hpp"
#include "runyard/domain/error.hpp"
#include "runyard/support/config.hpp"

namespace runyard {
Json kubernetes_job(const KubernetesConfig &config, const Launch &launch) {
  const auto &a = launch.assignment.attempt;
  const auto &s = launch.assignment.spec;
  Json labels = {{"app.kubernetes.io/managed-by", "runyard"},
                 {"runyard.attempt", a.id},
                 {"runyard.run", a.run_id}};
  Json environment = Json::array();
  auto env = [&](const std::string &key, const std::string &value) {
    environment.push_back({{"name", key}, {"value", value}});
  };
  env("RUNYARD_ATTEMPT_ID", a.id);
  env("RUNYARD_RUN_ID", a.run_id);
  env("RUNYARD_GENERATION", std::to_string(a.generation));
  env("RUNYARD_CAPABILITY", launch.capability);
  env("RUNYARD_COORDINATOR", config.coordinator);
  if (config.development)
    env("RUNYARD_PROFILE", "development");
  Json resources = {{"cpu", std::to_string(s.resources.cpu_millis) + "m"},
                    {"memory", std::to_string(s.resources.memory_mib) + "Mi"}};
  Json container = {
      {"name", "experiment"},
      {"image", s.image},
      {"imagePullPolicy", "IfNotPresent"},
      {"command", Json::array({"/usr/local/bin/runyard-runner"})},
      {"env", environment},
      {"resources", {{"requests", resources}, {"limits", resources}}},
      {"securityContext",
       {{"allowPrivilegeEscalation", false}, {"capabilities", {{"drop", Json::array({"ALL"})}}}}}};
  Json pod = {{"restartPolicy", "Never"},
              {"automountServiceAccountToken", false},
              {"serviceAccountName", "workload"},
              {"terminationGracePeriodSeconds", 15},
              {"securityContext",
               {{"runAsNonRoot", true},
                {"runAsUser", 10001},
                {"runAsGroup", 10001},
                {"seccompProfile", {{"type", "RuntimeDefault"}}}}}};
  if (!config.runner_ca_configmap.empty()) {
    container["env"].push_back({{"name", "RUNYARD_TLS_CA"}, {"value", "/etc/runyard/ca/ca.crt"}});
    container["volumeMounts"] =
        Json::array({{{"name", "ca"}, {"mountPath", "/etc/runyard/ca"}, {"readOnly", true}}});
    pod["volumes"] =
        Json::array({{{"name", "ca"}, {"configMap", {{"name", config.runner_ca_configmap}}}}});
  }
  pod["containers"] = Json::array({container});
  return {{"apiVersion", "batch/v1"},
          {"kind", "Job"},
          {"metadata",
           {{"name", "runyard-" + a.id}, {"namespace", config.name_space}, {"labels", labels}}},
          {"spec",
           {{"completions", 1},
            {"parallelism", 1},
            {"backoffLimit", 0},
            {"template", {{"metadata", {{"labels", labels}}}, {"spec", pod}}}}}};
}
std::string KubernetesBackend::jobs() const {
  return "/apis/batch/v1/namespaces/" + HttpClient::escape(config_.name_space) + "/jobs";
}
HttpResult KubernetesBackend::call(const std::string &method, const std::string &path,
                                   const Json &body) const {
  auto token = read_file(config_.token_file);
  while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back())))
    token.pop_back();
  HttpClient http({.ca_file = config_.api_ca, .bearer = token, .timeout_seconds = 10});
  auto result = http.request(method, config_.api + path, body.is_null() ? "" : body.dump());
  if (result.status >= 500 || result.status == 429)
    throw Error(ErrorCode::unavailable, "Kubernetes API unavailable");
  return result;
}
std::vector<std::string> KubernetesBackend::inventory() {
  std::vector<std::string> result;
  std::string cursor;
  do {
    auto response = call(
        "GET", jobs() +
                   "?labelSelector=app.kubernetes.io%2Fmanaged-by%3Drunyard&limit=100&continue=" +
                   HttpClient::escape(cursor));
    if (response.status != 200)
      throw Error(ErrorCode::unavailable, "cannot list Kubernetes Jobs");
    auto body = Json::parse(response.body);
    for (const auto &job : body.at("items"))
      result.push_back(job.at("metadata").at("labels").at("runyard.attempt"));
    cursor = body.at("metadata").value("continue", "");
  } while (!cursor.empty());
  return result;
}
std::string KubernetesBackend::ensure(const Launch &launch) {
  auto name = "runyard-" + launch.assignment.attempt.id;
  auto path = jobs() + "/" + name;
  auto response = call("GET", path);
  if (response.status == 404) {
    response = call("POST", jobs(), kubernetes_job(config_, launch));
    if (response.status == 409)
      response = call("GET", path);
  }
  if (response.status != 200 && response.status != 201)
    throw Error(ErrorCode::unavailable, "cannot reconcile Kubernetes Job");
  auto job = Json::parse(response.body);
  if (job.at("metadata").at("labels").value("runyard.attempt", "") != launch.assignment.attempt.id)
    throw Error(ErrorCode::conflict, "Kubernetes Job name belongs to another resource");
  return job.at("metadata").at("uid");
}
void KubernetesBackend::remove(const std::string &id) {
  auto path = jobs() + "/runyard-" + id;
  auto response =
      call("DELETE", path,
           {{"apiVersion", "v1"}, {"kind", "DeleteOptions"}, {"propagationPolicy", "Foreground"}});
  if (response.status != 200 && response.status != 202 && response.status != 404)
    throw Error(ErrorCode::unavailable, "cannot delete Kubernetes Job");
  if (call("GET", path).status != 404)
    throw Error(ErrorCode::unavailable, "Kubernetes cleanup pending");
  auto pods = call("GET", "/api/v1/namespaces/" + HttpClient::escape(config_.name_space) +
                              "/pods?labelSelector=runyard.attempt%3D" + HttpClient::escape(id));
  if (pods.status != 200 || !Json::parse(pods.body).at("items").empty())
    throw Error(ErrorCode::unavailable, "Kubernetes Pod cleanup pending");
}
} // namespace runyard
