#pragma once
#include <cstdint>
#include <map>
#include <string>

namespace runyard {
struct HttpResult {
  long status{};
  std::string body;
};
struct HttpOptions {
  std::string unix_socket{};
  std::string ca_file{};
  std::string bearer{};
  int timeout_seconds{30};
  std::size_t max_response_bytes{16 * 1024 * 1024};
};
class HttpClient {
public:
  explicit HttpClient(HttpOptions options = {}) : options_(std::move(options)) {}
  HttpResult request(const std::string &method, const std::string &url,
                     const std::string &body = "",
                     const std::map<std::string, std::string> &headers = {}) const;
  void download(const std::string &url, const std::string &file, std::uint64_t expected_size) const;
  static std::string escape(const std::string &value);

private:
  HttpOptions options_;
};
} // namespace runyard
