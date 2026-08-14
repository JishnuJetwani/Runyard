#include "runyard/http/api.hpp"
#include "runyard/domain/error.hpp"
#include "runyard/support/crypto.hpp"
#include <spdlog/spdlog.h>

namespace runyard {
namespace {
int number(const drogon::HttpRequestPtr &r, const std::string &key, int fallback) {
  auto value = r->getParameter(key);
  if (value.empty())
    return fallback;
  try {
    std::size_t used;
    int result = std::stoi(value, &used);
    if (used == value.size() && result >= 0)
      return result;
  } catch (...) {
  }
  throw Error(ErrorCode::invalid, "invalid numeric query parameter: " + key);
}
std::pair<int, std::string> error_status(ErrorCode code) {
  switch (code) {
  case ErrorCode::invalid:
    return {400, "invalid_request"};
  case ErrorCode::not_found:
    return {404, "not_found"};
  case ErrorCode::conflict:
    return {409, "conflict"};
  case ErrorCode::unauthorized:
    return {401, "unauthorized"};
  case ErrorCode::stale:
    return {409, "stale_attempt"};
  case ErrorCode::exhausted:
    return {429, "resource_exhausted"};
  default:
    return {503, "unavailable"};
  }
}
} // namespace
drogon::HttpResponsePtr json_response(const Json &body, int status, const std::string &id) {
  auto response = drogon::HttpResponse::newHttpResponse();
  response->setStatusCode(static_cast<drogon::HttpStatusCode>(status));
  response->setContentTypeCode(drogon::CT_APPLICATION_JSON);
  response->setBody(body.dump());
  if (!id.empty())
    response->addHeader("X-Request-ID", id);
  return response;
}
void Api::dispatch(const drogon::HttpRequestPtr &request, HttpCallback callback,
                   std::function<Json()> work) {
  dispatch_response(request, std::move(callback),
                    [work = std::move(work)] { return json_response(work()); });
}
void Api::dispatch_response(const drogon::HttpRequestPtr &request, HttpCallback callback,
                            std::function<drogon::HttpResponsePtr()> work) {
  auto id = random_id();
  if (!constant_equal(request->getHeader("authorization"), "Bearer " + token_)) {
    callback(json_response({{"error",
                             {{"code", "unauthorized"},
                              {"message", "valid owner credentials required"},
                              {"request_id", id}}}},
                           401, id));
    return;
  }
  if (!ready_()) {
    callback(json_response(
        {{"error",
          {{"code", "unavailable"}, {"message", "coordinator is not ready"}, {"request_id", id}}}},
        503, id));
    return;
  }
  if (!executor_.submit([callback, work = std::move(work), id, path = request->path()] {
        try {
          auto response = work();
          response->addHeader("X-Request-ID", id);
          spdlog::info("{}", Json{{"event", "http_request"},
                                  {"request_id", id},
                                  {"path", path},
                                  {"status", static_cast<int>(response->statusCode())}}
                                 .dump());
          callback(response);
        } catch (const Error &e) {
          auto [status, code] = error_status(e.code());
          callback(json_response(
              {{"error", {{"code", code}, {"message", e.what()}, {"request_id", id}}}}, status,
              id));
        } catch (const Json::exception &) {
          callback(json_response({{"error",
                                   {{"code", "invalid_request"},
                                    {"message", "malformed JSON request"},
                                    {"request_id", id}}}},
                                 400, id));
        } catch (const std::exception &e) {
          spdlog::error(
              "{}",
              Json{{"event", "http_error"}, {"request_id", id}, {"message", e.what()}}.dump());
          callback(json_response({{"error",
                                   {{"code", "unavailable"},
                                    {"message", "operation temporarily unavailable"},
                                    {"request_id", id}}}},
                                 503, id));
        }
      }))
    callback(json_response(
        {{"error",
          {{"code", "unavailable"}, {"message", "request queue is full"}, {"request_id", id}}}},
        503, id));
}
void Api::mount() {
  auto &app = drogon::app();
  app.registerHandler("/v1/sweeps",
                      [this](const drogon::HttpRequestPtr &r, HttpCallback &&cb) {
                        dispatch(r, std::move(cb), [this, r] {
                          auto spec = decode_sweep(Json::parse(r->body()));
                          return encode(runs_.sweep(spec, r->getHeader("idempotency-key"),
                                                    sha256(encode(spec).dump())));
                        });
                      },
                      {drogon::Post});
  app.registerHandler("/v1/sweeps/{1}",
                      [this](const drogon::HttpRequestPtr &r, HttpCallback &&cb, std::string id) {
                        dispatch(r, std::move(cb),
                                 [this, id] { return encode(runs_.get_sweep(id)); });
                      },
                      {drogon::Get});
  app.registerHandler("/v1/runs/{1}/rerun",
                      [this](const drogon::HttpRequestPtr &r, HttpCallback &&cb, std::string id) {
                        dispatch(r, std::move(cb), [this, r, id] {
                          return encode(runs_.rerun(id, r->getHeader("idempotency-key")));
                        });
                      },
                      {drogon::Post});
  app.registerHandler("/v1/workers",
                      [this](const drogon::HttpRequestPtr &r, HttpCallback &&cb) {
                        dispatch(r, std::move(cb),
                                 [this] { return Json{{"items", encode_list(runs_.workers())}}; });
                      },
                      {drogon::Get});
  app.registerHandler("/v1/workers/{1}/drain",
                      [this](const drogon::HttpRequestPtr &r, HttpCallback &&cb, std::string id) {
                        dispatch(r, std::move(cb), [this, r, id] {
                          auto body = Json::parse(r->body());
                          runs_.drain_worker(id, body.value("drained", true));
                          return Json{{"id", id}, {"drained", body.value("drained", true)}};
                        });
                      },
                      {drogon::Post});
  app.registerHandler("/v1/runs/{1}/cancel",
                      [this](const drogon::HttpRequestPtr &r, HttpCallback &&cb, std::string id) {
                        dispatch(r, std::move(cb), [this, id] { return encode(runs_.cancel(id)); });
                      },
                      {drogon::Post});
  app.registerHandler(
      "/v1/runs/{1}/artifacts",
      [this](const drogon::HttpRequestPtr &r, HttpCallback &&cb, std::string id) {
        dispatch(r, std::move(cb), [this, r, id] {
          return Json{{"items", encode_list(artifacts_.list(id, r->getParameter("attempt")))}};
        });
      },
      {drogon::Get});
  app.registerHandler("/v1/artifacts/{1}",
                      [this](const drogon::HttpRequestPtr &r, HttpCallback &&cb, std::string id) {
                        dispatch(r, std::move(cb),
                                 [this, id] { return encode(artifacts_.get(id)); });
                      },
                      {drogon::Get});
  app.registerHandler("/v1/artifacts/{1}/download",
                      [this](const drogon::HttpRequestPtr &r, HttpCallback &&cb, std::string id) {
                        dispatch_response(r, std::move(cb), [this, id] {
                          return drogon::HttpResponse::newFileResponse(
                              artifacts_.download(id).string(), "artifact.bin",
                              drogon::CT_APPLICATION_OCTET_STREAM);
                        });
                      },
                      {drogon::Get});

  for (const auto &kind : {std::string("logs"), std::string("metrics")}) {
    app.registerHandler(
        "/v1/runs/{1}/" + kind,
        [this, kind](const drogon::HttpRequestPtr &r, HttpCallback &&cb, std::string id) {
          dispatch(r, std::move(cb), [this, r, id, kind] {
            auto run = runs_.get(id);
            auto attempt = r->getParameter("attempt");
            if (attempt.empty())
              attempt = run.active_attempt;
            auto rows =
                runs_.telemetry(id, attempt, number(r, "after", 0), number(r, "limit", 100),
                                kind == "logs" ? "logs" : "metric", r->getParameter("name"));
            return Json{
                {"attempt_id", attempt},
                {"items", encode_list(rows)},
                {"next_cursor", rows.empty() ? number(r, "after", 0) : rows.back().sequence}};
          });
        },
        {drogon::Get});
  }

  app.registerHandler("/health/live",
                      [](const drogon::HttpRequestPtr &, HttpCallback &&cb) {
                        cb(json_response({{"status", "live"}}));
                      },
                      {drogon::Get});
  app.registerHandler("/health/ready",
                      [this](const drogon::HttpRequestPtr &, HttpCallback &&cb) {
                        bool ready = ready_();
                        cb(json_response({{"ready", ready}}, ready ? 200 : 503));
                      },
                      {drogon::Get});
  app.registerHandler("/v1/runs",
                      [this](const drogon::HttpRequestPtr &r, HttpCallback &&cb) {
                        dispatch(r, std::move(cb), [this, r] {
                          auto spec = decode_spec(Json::parse(r->body()));
                          return encode(runs_.submit(spec, r->getHeader("idempotency-key"),
                                                     sha256(encode(spec).dump())));
                        });
                      },
                      {drogon::Post});
  app.registerHandler("/v1/runs",
                      [this](const drogon::HttpRequestPtr &r, HttpCallback &&cb) {
                        dispatch(r, std::move(cb), [this, r] {
                          auto values = runs_.list(number(r, "limit", 50), r->getParameter("after"),
                                                   r->getParameter("status"));
                          return Json{{"items", encode_list(values)},
                                      {"next_cursor", values.empty() ? "" : values.back().id}};
                        });
                      },
                      {drogon::Get});
  app.registerHandler("/v1/runs/{1}",
                      [this](const drogon::HttpRequestPtr &r, HttpCallback &&cb, std::string id) {
                        dispatch(r, std::move(cb), [this, id] { return encode(runs_.get(id)); });
                      },
                      {drogon::Get});
  app.registerHandler("/v1/runs/{1}/attempts",
                      [this](const drogon::HttpRequestPtr &r, HttpCallback &&cb, std::string id) {
                        dispatch(r, std::move(cb), [this, id] {
                          return Json{{"items", encode_list(runs_.attempts(id))}};
                        });
                      },
                      {drogon::Get});
  app.registerHandler("/v1/runs/{1}/events",
                      [this](const drogon::HttpRequestPtr &r, HttpCallback &&cb, std::string id) {
                        dispatch(r, std::move(cb), [this, id, r] {
                          return Json{
                              {"items", encode_list(runs_.events(id, number(r, "after", 0),
                                                                 number(r, "limit", 100)))}};
                        });
                      },
                      {drogon::Get});
}
} // namespace runyard
