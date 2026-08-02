#pragma once
#include "runyard/application/artifacts.hpp"
#include "runyard/application/runs.hpp"
#include "runyard/serialization/json.hpp"
#include "runyard/support/executor.hpp"
#include <drogon/drogon.h>

namespace runyard {
using HttpCallback = std::function<void(const drogon::HttpResponsePtr &)>;
class Api {
public:
  Api(RunService &runs, ArtifactService &artifacts, Executor &executor, std::string token,
      std::function<bool()> ready)
      : runs_(runs), artifacts_(artifacts), executor_(executor), token_(std::move(token)),
        ready_(std::move(ready)) {}
  void mount();

private:
  void dispatch(const drogon::HttpRequestPtr &request, HttpCallback callback,
                std::function<Json()> work);
  void dispatch_response(const drogon::HttpRequestPtr &, HttpCallback,
                         std::function<drogon::HttpResponsePtr()>);
  RunService &runs_;
  ArtifactService &artifacts_;
  Executor &executor_;
  std::string token_;
  std::function<bool()> ready_;
};
drogon::HttpResponsePtr json_response(const Json &body, int status = 200,
                                      const std::string &request_id = "");
} // namespace runyard
