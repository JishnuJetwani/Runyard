#include "runyard/cli/workflows.hpp"
#include "runyard/support/crypto.hpp"
#include <filesystem>
#include <iostream>
#include <thread>

namespace runyard {
void show_logs(const Client &client, const std::string &run, std::string attempt, bool follow,
               bool json) {
  const auto path = "/v1/runs/" + HttpClient::escape(run);
  const bool fixed = !attempt.empty();
  std::int64_t cursor = 0;
  bool terminal_seen = false;
  for (;;) {
    auto current = client.get(path);
    auto selected = current.at("active_attempt").get<std::string>();
    if (!fixed && selected != attempt) {
      attempt = selected;
      cursor = 0;
      terminal_seen = false;
    }
    auto page = client.get(path + "/logs?attempt=" + HttpClient::escape(attempt) +
                           "&limit=100&after=" + std::to_string(cursor));
    for (const auto &item : page.at("items")) {
      if (json)
        std::cout << Json{{"attempt_id", attempt}, {"record", item}}.dump() << '\n';
      else
        std::cout << item.at("text").get<std::string>();
    }
    std::cout.flush();
    cursor = page.at("next_cursor").get<std::int64_t>();
    if (page.at("items").size() == 100)
      continue;
    if (!follow || (terminal_seen && page.at("items").empty()))
      break;
    // Read once more after seeing a terminal state to include concurrently persisted output.
    terminal_seen = terminal(parse_status(current.at("status"))) || (fixed && selected != attempt);
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}
namespace {
std::string csv(const std::string &value) {
  std::string out = "\"";
  for (char c : value) {
    if (c == '"')
      out += '"';
    out += c;
  }
  return out + '"';
}
} // namespace
void export_metrics(const Client &client, const std::string &run, const std::string &attempt,
                    const std::string &name, const std::string &format) {
  const auto path = "/v1/runs/" + HttpClient::escape(run);
  auto selected =
      attempt.empty() ? client.get(path).at("active_attempt").get<std::string>() : attempt;
  std::int64_t cursor = 0;
  if (format == "csv")
    std::cout << "attempt_id,sequence,name,step,value,timestamp_ms\n";
  for (;;) {
    auto page = client.get(path + "/metrics?attempt=" + HttpClient::escape(selected) + "&name=" +
                           HttpClient::escape(name) + "&limit=100&after=" + std::to_string(cursor));
    for (const auto &item : page.at("items")) {
      if (format == "csv")
        std::cout << csv(selected) << ',' << item["sequence"] << ',' << csv(item["name"]) << ','
                  << item["step"] << ',' << item["value"] << ',' << item["timestamp_ms"] << '\n';
      else {
        auto record = item;
        record["attempt_id"] = selected;
        std::cout << record.dump() << '\n';
      }
    }
    cursor = page.at("next_cursor").get<std::int64_t>();
    if (page.at("items").size() < 100)
      break;
  }
}
void download_artifact(const Client &client, const std::string &id,
                       const std::string &destination) {
  namespace fs = std::filesystem;
  if (fs::exists(destination))
    throw std::runtime_error("output already exists");
  auto metadata = client.get("/v1/artifacts/" + HttpClient::escape(id));
  auto temporary = destination + ".partial-" + random_id();
  try {
    client.download("/v1/artifacts/" + HttpClient::escape(id) + "/download", temporary,
                    metadata.at("size"));
    if (sha256_file(temporary) != metadata.at("sha256").get<std::string>())
      throw std::runtime_error("download checksum mismatch");
    // A hard link publishes atomically without overwriting a destination created during transfer.
    fs::create_hard_link(temporary, destination);
    fs::remove(temporary);
  } catch (...) {
    std::error_code ignored;
    fs::remove(temporary, ignored);
    throw;
  }
  std::cerr << "Saved " << destination << '\n';
}
} // namespace runyard
