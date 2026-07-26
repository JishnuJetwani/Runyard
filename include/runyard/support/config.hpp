#pragma once
#include "runyard/domain/model.hpp"
#include <string>

namespace runyard {
std::string env(const char *name, const std::string &fallback = "");
int env_int(const char *name, int fallback);
std::string read_file(const std::string &path);
struct ServerConfig {
  std::string database;
  std::string owner_token;
  std::string worker_token;
  std::string signing_key;
  std::string host;
  int http_port{8080};
  int grpc_port{9090};
  bool development{};
  std::string certificate;
  std::string private_key;
  std::string mode;
  std::string artifacts;
  Timing timing;
  static ServerConfig load();
};
} // namespace runyard
