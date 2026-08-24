#include "runyard/execution/docker.hpp"
#include "runyard/domain/error.hpp"
#include <set>
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
Json docker_container_spec(const DockerConfig &config, const Launch &launch) {
  const auto &a = launch.assignment.attempt;
  const auto &s = launch.assignment.spec;
  std::vector<std::string> environment{
      "RUNYARD_ATTEMPT_ID=" + a.id,
      "RUNYARD_RUN_ID=" + a.run_id,
      "RUNYARD_GENERATION=" + std::to_string(a.generation),
      "RUNYARD_CAPABILITY=" + launch.capability,
      "RUNYARD_COORDINATOR=" + config.coordinator,
      "RUNYARD_PROFILE=" + (config.development ? std::string("development") : "production")};
  Json host = {{"NetworkMode", config.network},
               {"NanoCpus", std::int64_t{s.resources.cpu_millis} * 1000000},
               {"Memory", std::int64_t{s.resources.memory_mib} * 1024 * 1024},
               {"MemorySwap", std::int64_t{s.resources.memory_mib} * 1024 * 1024},
               {"PidsLimit", 256},
               {"Init", true},
               {"CapDrop", {"ALL"}},
               {"SecurityOpt", {"no-new-privileges"}}};
  if (s.resources.gpu_count > 0) {
    std::vector<std::string> ids;
    for (const auto &allocation : a.gpu_allocations)
      ids.push_back(allocation.device.uuid);
    std::set<std::string> unique(ids.begin(), ids.end());
    if (ids.size() != static_cast<std::size_t>(s.resources.gpu_count) ||
        unique.size() != ids.size())
      throw Error(ErrorCode::invalid, "assignment requires exact, distinct GPU UUIDs");
    std::string visible;
    for (const auto &id : ids) {
      if (!id.starts_with("GPU-"))
        throw Error(ErrorCode::invalid, "invalid assigned GPU UUID");
      if (!visible.empty())
        visible += ',';
      visible += id;
    }
    host["DeviceRequests"] = Json::array({{{"Driver", "nvidia"},
                                           {"Count", 0},
                                           {"DeviceIDs", ids},
                                           {"Capabilities", Json::array({Json::array({"gpu"})})}}});
    environment.push_back("NVIDIA_VISIBLE_DEVICES=" + visible);
    environment.push_back("NVIDIA_DRIVER_CAPABILITIES=compute,utility");
  } else {
    if (!a.gpu_allocations.empty())
      throw Error(ErrorCode::invalid, "CPU assignment contains GPU allocations");
    environment.push_back("NVIDIA_VISIBLE_DEVICES=void");
  }
  if (!config.ca_host_path.empty()) {
    host["Binds"] = {config.ca_host_path + ":/runyard-ca.pem:ro"};
    environment.push_back("RUNYARD_TLS_CA=/runyard-ca.pem");
  }
  return {{"Image", s.image},
          {"Entrypoint", {"/usr/local/bin/runyard-runner"}},
          {"Cmd", Json::array()},
          {"Env", environment},
          {"User", "10001:10001"},
          {"Labels", {{"runyard.attempt", a.id}, {"runyard.worker", config.worker}}},
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
                                docker_container_spec(config_, launch).dump());
    expect(create, {201, 409});
    existing = http_.request("GET", endpoint(path + "/json"));
  }
  expect(existing, {200});
  auto container = Json::parse(existing.body);
  if (!docker_container_matches(container, config_, launch))
    throw Error(ErrorCode::conflict,
                "container identity or GPU allocation does not match assignment");
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

namespace runyard {
std::string DockerBackend::engine_id() {
  auto response = http_.request("GET", endpoint("/info"));
  expect(response, {200});
  auto id = Json::parse(response.body).value("ID", "");
  if (id.empty())
    throw Error(ErrorCode::unavailable, "Docker engine identity unavailable");
  return id;
}
bool docker_container_matches(const Json &container, const DockerConfig &config,
                              const Launch &launch) {
  auto expected = docker_container_spec(config, launch);
  const auto &actual = container.at("Config");
  if (actual.value("Image", "") != expected["Image"].get<std::string>() ||
      actual.value("Labels", Json::object()).value("runyard.attempt", "") !=
          launch.assignment.attempt.id ||
      actual.value("Labels", Json::object()).value("runyard.worker", "") != config.worker)
    return false;
  auto requests = container.value("HostConfig", Json::object()).value("DeviceRequests", Json());
  if (launch.assignment.spec.resources.gpu_count == 0)
    return requests.is_null() || (requests.is_array() && requests.empty());
  if (!requests.is_array() || requests.size() != 1)
    return false;
  const auto &wanted = expected["HostConfig"]["DeviceRequests"][0];
  const auto &found = requests[0];
  if (found.value("Driver", "") != "nvidia" || found.value("Count", -1) != 0 ||
      found.value("Capabilities", Json()) != wanted["Capabilities"])
    return false;
  auto ids = found.value("DeviceIDs", std::vector<std::string>{});
  auto required = wanted["DeviceIDs"].get<std::vector<std::string>>();
  std::sort(ids.begin(), ids.end());
  std::sort(required.begin(), required.end());
  return ids == required;
}
} // namespace runyard
