#include "runyard/execution/docker.hpp"
#include "runyard/domain/error.hpp"
#include <sstream>

namespace runyard {
namespace {
std::string endpoint(const std::string &path) { return "http://localhost/v1.44" + path; }
void expect(const HttpResult &response, std::initializer_list<long> allowed) {
  for (auto status : allowed)
    if (response.status == status)
      return;
  throw Error(ErrorCode::unavailable,
              "Docker operation returned HTTP " + std::to_string(response.status));
}
} // namespace
Json DockerBackend::container_spec(const Launch &launch) const {
  const auto &a = launch.assignment.attempt;
  const auto &s = launch.assignment.spec;
  std::vector<std::string> environment{
      "RUNYARD_ATTEMPT_ID=" + a.id,
      "RUNYARD_RUN_ID=" + a.run_id,
      "RUNYARD_GENERATION=" + std::to_string(a.generation),
      "RUNYARD_CAPABILITY=" + launch.capability,
      "RUNYARD_COORDINATOR=" + config_.coordinator,
      "RUNYARD_PROFILE=" + (config_.development ? std::string("development") : "production")};
  Json host = {{"NetworkMode", config_.network},
               {"NanoCpus", std::int64_t{s.resources.cpu_millis} * 1000000},
               {"Memory", std::int64_t{s.resources.memory_mib} * 1024 * 1024},
               {"MemorySwap", std::int64_t{s.resources.memory_mib} * 1024 * 1024},
               {"PidsLimit", 256},
               {"Init", true},
               {"CapDrop", {"ALL"}},
               {"SecurityOpt", {"no-new-privileges"}}};
  if (!config_.ca_host_path.empty()) {
    host["Binds"] = {config_.ca_host_path + ":/runyard-ca.pem:ro"};
    environment.push_back("RUNYARD_TLS_CA=/runyard-ca.pem");
  }
  return {{"Image", s.image},
          {"Entrypoint", {"/usr/local/bin/runyard-runner"}},
          {"Cmd", Json::array()},
          {"Env", environment},
          {"User", "10001:10001"},
          {"Labels", {{"runyard.attempt", a.id}, {"runyard.worker", config_.worker}}},
          {"HostConfig", host}};
}
std::string DockerBackend::ensure(const Launch &launch) {
  auto name = "runyard-" + launch.assignment.attempt.id;
  auto path = "/containers/" + name;
  auto existing = http_.request("GET", endpoint(path + "/json"));
  if (existing.status == 404) {
    auto local = http_.request(
        "GET", endpoint("/images/" + HttpClient::escape(launch.assignment.spec.image) + "/json"));
    if (local.status == 404) {
      auto pull = http_.request("POST", endpoint("/images/create?fromImage=" +
                                                 HttpClient::escape(launch.assignment.spec.image)));
      expect(pull, {200});
      std::istringstream lines(pull.body);
      std::string line;
      while (std::getline(lines, line))
        if (!line.empty() && Json::parse(line).contains("error"))
          throw Error(ErrorCode::unavailable, "Docker image pull failed");
    } else
      expect(local, {200});
    auto create = http_.request("POST", endpoint("/containers/create?name=" + name),
                                container_spec(launch).dump());
    expect(create, {201, 409});
    existing = http_.request("GET", endpoint(path + "/json"));
  }
  expect(existing, {200});
  auto container = Json::parse(existing.body);
  if (container["Config"]["Labels"].value("runyard.attempt", "") != launch.assignment.attempt.id ||
      container["Config"].value("Image", "") != launch.assignment.spec.image)
    throw Error(ErrorCode::conflict, "container identity does not match assignment");
  // An exited runner must never be restarted under the same attempt identity.
  if (container["State"].value("Status", "") == "created")
    expect(http_.request("POST", endpoint(path + "/start")), {204, 304});
  return container.at("Id").get<std::string>();
}
bool DockerBackend::has_stopped(const std::string &id) {
  auto response = http_.request("GET", endpoint("/containers/runyard-" + id + "/json"));
  if (response.status == 404)
    return true;
  expect(response, {200});
  auto state = Json::parse(response.body).at("State").value("Status", "");
  return state == "exited" || state == "dead";
}
void DockerBackend::remove(const std::string &id) {
  auto path = "/containers/runyard-" + id;
  expect(http_.request("POST", endpoint(path + "/stop?t=10")), {204, 304, 404});
  expect(http_.request("DELETE", endpoint(path + "?force=true&v=true")), {204, 404});
}
} // namespace runyard

namespace runyard {
std::vector<std::string> DockerBackend::inventory() {
  auto filter = Json{{"label", {"runyard.worker=" + config_.worker}}}.dump();
  auto response = http_.request(
      "GET", endpoint("/containers/json?all=true&filters=" + HttpClient::escape(filter)));
  expect(response, {200});
  std::vector<std::string> ids;
  for (const auto &container : Json::parse(response.body)) {
    auto id = container["Labels"].value("runyard.attempt", "");
    if (!id.empty())
      ids.push_back(id);
  }
  return ids;
}
} // namespace runyard
