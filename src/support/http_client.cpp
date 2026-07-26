#include "runyard/support/http_client.hpp"
#include "runyard/domain/error.hpp"
#include <curl/curl.h>
#include <memory>

namespace runyard {
namespace {
struct Buffer {
  std::string data;
  std::size_t limit;
};
std::size_t append(char *data, std::size_t size, std::size_t count, void *opaque) {
  auto &buffer = *static_cast<Buffer *>(opaque);
  auto bytes = size * count;
  if (bytes > buffer.limit - buffer.data.size())
    return 0;
  buffer.data.append(data, bytes);
  return bytes;
}
void initialize() {
  static const auto result = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (result != CURLE_OK)
    throw Error(ErrorCode::unavailable, "HTTP initialization failed");
}
} // namespace
HttpResult HttpClient::request(const std::string &method, const std::string &url,
                               const std::string &body,
                               const std::map<std::string, std::string> &headers) const {
  initialize();
  std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), curl_easy_cleanup);
  if (!handle)
    throw Error(ErrorCode::unavailable, "HTTP allocation failed");
  Buffer buffer{{}, options_.max_response_bytes};
  auto *curl = handle.get();
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(options_.timeout_seconds));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
  if (!options_.unix_socket.empty())
    curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, options_.unix_socket.c_str());
  if (!options_.ca_file.empty())
    curl_easy_setopt(curl, CURLOPT_CAINFO, options_.ca_file.c_str());
  if (method == "POST" || method == "PUT" || !body.empty()) {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
  }
  curl_slist *raw = nullptr;
  raw = curl_slist_append(raw, "Content-Type: application/json");
  if (!options_.bearer.empty())
    raw = curl_slist_append(raw, ("Authorization: Bearer " + options_.bearer).c_str());
  for (const auto &[key, value] : headers)
    raw = curl_slist_append(raw, (key + ": " + value).c_str());
  std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> list(raw, curl_slist_free_all);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list.get());
  auto code = curl_easy_perform(curl);
  if (code != CURLE_OK)
    throw Error(ErrorCode::unavailable,
                std::string("HTTP request failed: ") + curl_easy_strerror(code));
  long status{};
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  return {status, std::move(buffer.data)};
}
std::string HttpClient::escape(const std::string &value) {
  initialize();
  char *encoded = curl_easy_escape(nullptr, value.c_str(), static_cast<int>(value.size()));
  if (!encoded)
    throw Error(ErrorCode::invalid, "cannot encode URL component");
  std::string result(encoded);
  curl_free(encoded);
  return result;
}
} // namespace runyard
