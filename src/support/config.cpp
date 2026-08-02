#include "runyard/support/config.hpp"
#include "runyard/domain/error.hpp"
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace runyard {
std::string env(const char *name, const std::string &fallback) {
  const char *value = std::getenv(name);
  return value ? value : fallback;
}
int env_int(const char *name, int fallback) {
  auto value = env(name);
  if (value.empty())
    return fallback;
  std::size_t used{};
  int result{};
  try {
    result = std::stoi(value, &used);
  } catch (...) {
    throw Error(ErrorCode::invalid, std::string("invalid integer setting: ") + name);
  }
  if (used != value.size() || result < 1)
    throw Error(ErrorCode::invalid, std::string("invalid integer setting: ") + name);
  return result;
}
std::string read_file(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file)
    throw Error(ErrorCode::invalid, "cannot read file: " + path);
  return std::string(std::istreambuf_iterator<char>(file), {});
}
ServerConfig ServerConfig::load() {
  ServerConfig c;
  c.database = env("RUNYARD_DATABASE_URL");
  c.owner_token = env("RUNYARD_OWNER_TOKEN");
  c.worker_token = env("RUNYARD_WORKER_TOKEN");
  c.signing_key = env("RUNYARD_SIGNING_KEY");
  if (c.worker_token.size() < 16 || c.signing_key.size() < 32)
    throw Error(ErrorCode::invalid,
                "worker token (16+ characters) and signing key (32+ characters) required");
  c.development = env("RUNYARD_PROFILE") == "development";
  c.host = env("RUNYARD_BIND", "127.0.0.1");
  c.http_port = env_int("RUNYARD_HTTP_PORT", 8080);
  c.grpc_port = env_int("RUNYARD_GRPC_PORT", 9090);
  c.certificate = env("RUNYARD_TLS_CERT");
  c.private_key = env("RUNYARD_TLS_KEY");
  c.mode = env("RUNYARD_EXECUTION_MODE", "docker");
  c.artifacts = env("RUNYARD_ARTIFACT_ROOT", ".local/artifacts");
  c.timing.heartbeat_seconds = env_int("RUNYARD_HEARTBEAT_SECONDS", 5);
  c.timing.lease_seconds = env_int("RUNYARD_LEASE_SECONDS", 30);
  c.timing.worker_seconds = env_int("RUNYARD_WORKER_SECONDS", 15);
  c.timing.launch_seconds = env_int("RUNYARD_LAUNCH_SECONDS", 300);
  c.timing.retry_base_seconds = env_int("RUNYARD_RETRY_BASE_SECONDS", 5);
  c.timing.termination_seconds = env_int("RUNYARD_TERMINATION_SECONDS", 10);
  c.timing.finalization_seconds = env_int("RUNYARD_FINALIZATION_SECONDS", 300);
  if (c.database.empty() || c.owner_token.size() < 16)
    throw Error(ErrorCode::invalid,
                "database URL and owner token (at least 16 characters) are required");
  if (!c.development && (c.certificate.empty() || c.private_key.empty()))
    throw Error(ErrorCode::invalid, "TLS certificate/key required outside development profile");
  if (c.mode != "docker" && c.mode != "kubernetes")
    throw Error(ErrorCode::invalid, "execution mode must be docker or kubernetes");
  if (c.timing.lease_seconds <= c.timing.heartbeat_seconds + c.timing.termination_seconds + 2)
    throw Error(ErrorCode::invalid,
                "lease must exceed heartbeat, termination grace, and safety margin");
  return c;
}
} // namespace runyard
