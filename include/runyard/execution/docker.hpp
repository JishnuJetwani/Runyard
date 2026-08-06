#pragma once
#include "runyard/execution/backend.hpp"
#include "runyard/serialization/json.hpp"
#include "runyard/support/http_client.hpp"

namespace runyard {
struct DockerConfig {
  std::string socket;
  std::string network;
  std::string worker;
  std::string coordinator;
  std::string ca_host_path;
  bool development;
};
class DockerBackend final : public ExecutionBackend {
public:
  explicit DockerBackend(DockerConfig config)
      : config_(std::move(config)), http_({.unix_socket = config_.socket, .timeout_seconds = 300}) {
  }
  std::vector<std::string> inventory() override;
  std::string ensure(const Launch &) override;
  void remove(const std::string &attempt_id) override;

private:
  Json container_spec(const Launch &) const;
  DockerConfig config_;
  HttpClient http_;
};
} // namespace runyard
