#include "runyard/cli/client.hpp"
#include "runyard/support/config.hpp"
#include "runyard/support/crypto.hpp"
#include <CLI/CLI.hpp>
#include <iostream>

int main(int argc, char **argv) {
  CLI::App app{"Runyard experiment client"};
  app.require_subcommand();
  std::string url = runyard::env("RUNYARD_URL", "http://127.0.0.1:8080"),
              token = runyard::env("RUNYARD_OWNER_TOKEN"), ca = runyard::env("RUNYARD_TLS_CA");
  bool json = false;
  app.add_option("--url", url);
  app.add_option("--ca", ca);
  app.add_flag("--json", json);
  auto *submit = app.add_subcommand("submit", "Submit an immutable experiment specification");
  std::string file, key;
  submit->add_option("file", file)->required();
  submit->add_option("--request-id", key);
  submit->fallthrough();
  auto *runs = app.add_subcommand("runs", "Inspect experiments");
  runs->require_subcommand();
  runs->fallthrough();
  auto *list = runs->add_subcommand("list", "List runs");
  int limit = 50;
  std::string after, status;
  list->add_option("--limit", limit);
  list->add_option("--after", after);
  list->add_option("--status", status);
  list->fallthrough();
  auto *get = runs->add_subcommand("get", "Inspect a run");
  std::string id;
  bool attempts = false, events = false;
  get->add_option("id", id)->required();
  get->add_flag("--attempts", attempts);
  get->add_flag("--events", events);
  get->fallthrough();
  CLI11_PARSE(app, argc, argv);
  try {
    if (token.empty())
      throw std::runtime_error("set RUNYARD_OWNER_TOKEN");
    runyard::Client client(url, token, ca);
    runyard::Json result;
    if (*submit) {
      auto body = runyard::Json::parse(runyard::read_file(file));
      runyard::decode_spec(body);
      if (key.empty())
        key = runyard::random_id();
      std::cerr << "Request ID: " << key << '\n';
      result = client.post("/v1/runs", body, key);
    } else if (*list)
      result = client.get("/v1/runs?limit=" + std::to_string(limit) +
                          "&after=" + runyard::HttpClient::escape(after) +
                          "&status=" + runyard::HttpClient::escape(status));
    else if (*get)
      result = client.get("/v1/runs/" + runyard::HttpClient::escape(id) +
                          (attempts ? "/attempts"
                           : events ? "/events"
                                    : ""));
    runyard::print_result(result, json);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "runyard: " << e.what() << '\n';
    return 1;
  }
}
