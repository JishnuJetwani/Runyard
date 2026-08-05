#pragma once
#include "runyard/domain/model.hpp"
#include <nlohmann/json.hpp>

namespace runyard {
using Json = nlohmann::json;
Json encode(const RunSpec &value);
Json encode(const SweepSpec &value);
Json encode(const Sweep &value);
SweepSpec decode_sweep(const Json &value);
Json encode(const Run &value);
Json encode(const Attempt &value);
Json encode(const Worker &value);
Json encode(const Telemetry &value);
Json encode(const Artifact &value);
Json encode(const Event &value);
RunSpec decode_spec(const Json &value);
Scalar decode_scalar(const Json &value);
Telemetry decode_telemetry(const Json &value);
template <class T> Json encode_list(const std::vector<T> &values) {
  auto result = Json::array();
  for (const auto &value : values)
    result.push_back(encode(value));
  return result;
}
} // namespace runyard
