#include "runyard/cli/client.hpp"
#include "runyard/domain/error.hpp"
#include <chrono>
#include <iostream>
#include <thread>

namespace runyard {
Client::Client(std::string url, std::string token, std::string ca_file)
    : url_(std::move(url)), http_({.ca_file = std::move(ca_file), .bearer = std::move(token)}) {
  while (url_.ends_with('/'))
    url_.pop_back();
}
Json Client::call(const std::string &method, const std::string &path, const std::string &body,
                  const std::string &key) const {
  for (int attempt = 0; attempt < 3; ++attempt) {
    try {
      auto response =
          http_.request(method, url_ + path, body,
                        key.empty() ? std::map<std::string, std::string>{}
                                    : std::map<std::string, std::string>{{"Idempotency-Key", key}});
      if (response.status == 503)
        throw Error(ErrorCode::unavailable, "coordinator temporarily unavailable");
      auto value = Json::parse(response.body);
      if (response.status >= 400)
        throw Error(ErrorCode::invalid, value.contains("error")
                                            ? value["error"].value("message", "request failed")
                                            : "request failed");
      return value;
    } catch (const Error &e) {
      if (e.code() != ErrorCode::unavailable || attempt == 2)
        throw;
      std::this_thread::sleep_for(std::chrono::milliseconds(200 * (attempt + 1)));
    }
  }
  throw Error(ErrorCode::unavailable, "request failed");
}
Json Client::get(const std::string &path) const { return call("GET", path, "", ""); }
Json Client::post(const std::string &path, const Json &body, const std::string &key) const {
  return call("POST", path, body.dump(), key);
}
} // namespace runyard
