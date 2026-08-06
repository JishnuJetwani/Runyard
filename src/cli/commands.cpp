#include "runyard/cli/commands.hpp"
#include "runyard/cli/workflows.hpp"
#include "runyard/support/config.hpp"
#include "runyard/support/crypto.hpp"
#include <CLI/CLI.hpp>
#include <iostream>

namespace runyard {
int command_line(int argc, char **argv) {
  CLI::App app{"Runyard experiment client"};
  app.require_subcommand(1);
  std::string url = env("RUNYARD_URL", "http://127.0.0.1:8080"), ca = env("RUNYARD_TLS_CA");
  bool json = false;
  app.add_option("--url", url);
  app.add_option("--ca", ca);
  app.add_flag("--json", json);
  auto sub = [](CLI::App *parent, const std::string &name, const std::string &description) {
    auto *child = parent->add_subcommand(name, description);
    child->fallthrough();
    return child;
  };
  auto *submit = sub(&app, "submit", "Submit an immutable specification");
  auto *sweep = sub(&app, "sweep", "Submit a parameter grid");
  std::string file, key, id, attempt, after, status, name, format = "json", output;
  bool attempts = false, events = false, follow = false, resume = false;
  int limit = 50;
  for (auto *command : {submit, sweep}) {
    command->add_option("file", file)->required();
    command->add_option("--request-id", key);
  }
  auto *runs = sub(&app, "runs", "Inspect experiments");
  runs->require_subcommand(1);
  auto *list = sub(runs, "list", "List runs");
  list->add_option("--limit", limit)->check(CLI::Range(1, 100));
  list->add_option("--after", after);
  list->add_option("--status", status);
  auto *get = sub(runs, "get", "Inspect a run");
  get->add_option("id", id)->required();
  get->add_flag("--attempts", attempts);
  get->add_flag("--events", events);
  auto *sweeps = sub(&app, "sweeps", "Inspect parameter sweeps");
  sweeps->require_subcommand(1);
  auto *sweep_get = sub(sweeps, "get", "Inspect a sweep");
  sweep_get->add_option("id", id)->required();
  auto *cancel = sub(&app, "cancel", "Cancel a run and request runtime cleanup");
  cancel->add_option("id", id)->required();
  auto *rerun = sub(&app, "rerun", "Create a new run linked to a terminal run");
  rerun->add_option("id", id)->required();
  rerun->add_option("--request-id", key);
  auto *logs = sub(&app, "logs", "Read logs; --json emits JSONL records");
  logs->add_option("id", id)->required();
  logs->add_option("--attempt", attempt);
  logs->add_flag("--follow", follow);
  auto *metrics = sub(&app, "metrics", "Export metric samples");
  metrics->add_option("id", id)->required();
  metrics->add_option("--attempt", attempt);
  metrics->add_option("--name", name);
  metrics->add_option("--format", format)->check(CLI::IsMember({"json", "csv"}));
  auto *artifacts = sub(&app, "artifacts", "Retrieve experiment outputs");
  artifacts->require_subcommand(1);
  auto *artifact_list = sub(artifacts, "list", "List a run's artifacts");
  artifact_list->add_option("id", id)->required();
  artifact_list->add_option("--attempt", attempt);
  auto *download = sub(artifacts, "download", "Download one artifact and verify its checksum");
  download->add_option("id", id)->required();
  download->add_option("--output,-o", output)->required();
  auto *workers = sub(&app, "workers", "Inspect worker capacity");
  workers->require_subcommand(1);
  auto *worker_list = sub(workers, "list", "List workers");
  auto *drain = sub(workers, "drain", "Stop assigning new work to a worker");
  drain->add_option("id", id)->required();
  drain->add_flag("--resume", resume);
  CLI11_PARSE(app, argc, argv);
  try {
    auto token = env("RUNYARD_OWNER_TOKEN");
    if (token.empty())
      throw std::runtime_error("set RUNYARD_OWNER_TOKEN");
    Client client(url, token, ca);
    auto path = "/v1/runs/" + HttpClient::escape(id);
    auto request_key = [&] {
      if (key.empty())
        key = random_id();
      std::cerr << "Request ID: " << key << '\n';
      return key;
    };
    Json result;
    if (*submit || *sweep) {
      auto body = Json::parse(read_file(file));
      if (*submit)
        decode_spec(body);
      else {
        auto spec = decode_sweep(body);
        expand_sweep(spec.base, spec.grid);
      }
      result = client.post(*submit ? "/v1/runs" : "/v1/sweeps", body, request_key());
    } else if (*list)
      result = client.get("/v1/runs?limit=" + std::to_string(limit) + "&after=" +
                          HttpClient::escape(after) + "&status=" + HttpClient::escape(status));
    else if (*get)
      result = client.get(path + (attempts ? "/attempts" : events ? "/events" : ""));
    else if (*sweep_get)
      result = client.get("/v1/sweeps/" + HttpClient::escape(id));
    else if (*cancel)
      result = client.post(path + "/cancel", Json::object(), "");
    else if (*rerun)
      result = client.post(path + "/rerun", Json::object(), request_key());
    else if (*logs) {
      show_logs(client, id, attempt, follow, json);
      return 0;
    } else if (*metrics) {
      export_metrics(client, id, attempt, name, format);
      return 0;
    } else if (*artifact_list)
      result = client.get(path + "/artifacts?attempt=" + HttpClient::escape(attempt));
    else if (*download) {
      download_artifact(client, id, output);
      return 0;
    } else if (*worker_list)
      result = client.get("/v1/workers");
    else if (*drain)
      result = client.post("/v1/workers/" + HttpClient::escape(id) + "/drain",
                           {{"drained", !resume}}, "");
    print_result(result, json);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "runyard: " << e.what() << '\n';
    return 1;
  }
}
} // namespace runyard
