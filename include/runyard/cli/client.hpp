#pragma once
#include "runyard/serialization/json.hpp"
#include "runyard/support/http_client.hpp"

namespace runyard {
class Client {
public:
  Client(std::string url, std::string token, std::string ca_file = "");
  Json get(const std::string &path) const;
  Json post(const std::string &path, const Json &body, const std::string &key) const;

  void download(const std::string &path, const std::string &file, std::uint64_t size) const {
    http_.download(url_ + path, file, size);
  }

private:
  Json call(const std::string &method, const std::string &path, const std::string &body,
            const std::string &key) const;
  std::string url_;
  HttpClient http_;
};
void print_result(const Json &value, bool json);
} // namespace runyard
