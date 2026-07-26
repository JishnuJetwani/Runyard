#pragma once
#include "runyard/application/runs.hpp"
#include "runyard/serialization/json.hpp"
#include "runyard/support/executor.hpp"
#include <drogon/drogon.h>

namespace runyard {
using HttpCallback = std::function<void(const drogon::HttpResponsePtr &)>;
class Api {
public:
  Api(RunService &runs, Executor &executor, std::string token, std::function<bool()> ready)
      : runs_(runs), executor_(executor), token_(std::move(token)), ready_(std::move(ready)) {}
  void mount();

private:
  void dispatch(const drogon::HttpRequestPtr &request, HttpCallback callback,
                std::function<Json()> work);
  RunService &runs_;
  Executor &executor_;
  std::string token_;
  std::function<bool()> ready_;
};
drogon::HttpResponsePtr json_response(const Json &body, int status = 200,
                                      const std::string &request_id = "");
} // namespace runyard
