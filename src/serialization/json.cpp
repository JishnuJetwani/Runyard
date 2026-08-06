#include "runyard/serialization/json.hpp"
#include "runyard/domain/error.hpp"
#include <set>

namespace runyard {
Scalar decode_scalar(const Json &j) {
  if (j.is_null())
    return nullptr;
  if (j.is_boolean())
    return j.get<bool>();
  if (j.is_number_integer()) {
    if (j.is_number_unsigned() && j.get<std::uint64_t>() > INT64_MAX)
      throw Error(ErrorCode::invalid, "integer parameter is too large");
    return j.get<std::int64_t>();
  }
  if (j.is_number_float())
    return j.get<double>();
  if (j.is_string())
    return j.get<std::string>();
  throw Error(ErrorCode::invalid, "parameters must be JSON scalars");
}
Json encode(const RunSpec &s) {
  Json p = Json::object();
  for (const auto &[key, value] : s.parameters)
    std::visit([&](const auto &v) { p[key] = v; }, value);
  return {{"version", s.version},
          {"name", s.name},
          {"image", s.image},
          {"command", s.command},
          {"parameters", p},
          {"environment", s.environment},
          {"labels", s.labels},
          {"resources",
           {{"cpu_millis", s.resources.cpu_millis}, {"memory_mib", s.resources.memory_mib}}},
          {"retry",
           {{"max_attempts", s.retry.max_attempts},
            {"retry_exit", s.retry.retry_exit},
            {"retry_timeout", s.retry.retry_timeout}}},
          {"timeout_seconds", s.timeout_seconds},
          {"priority", s.priority},
          {"source", {{"repository", s.source_repository}, {"revision", s.source_revision}}}};
}
RunSpec decode_spec(const Json &j) {
  try {
    if (!j.is_object())
      throw Error(ErrorCode::invalid, "specification must be an object");
    const std::set<std::string> allowed{
        "version", "name",      "image", "command",         "parameters", "environment",
        "labels",  "resources", "retry", "timeout_seconds", "priority",   "source"};
    for (auto it = j.begin(); it != j.end(); ++it)
      if (!allowed.contains(it.key()))
        throw Error(ErrorCode::invalid, "unknown specification field: " + it.key());
    RunSpec s;
    s.version = j.value("version", 1);
    s.name = j.at("name").get<std::string>();
    s.image = j.at("image").get<std::string>();
    s.command = j.at("command").get<std::vector<std::string>>();
    auto p = j.value("parameters", Json::object());
    if (!p.is_object())
      throw Error(ErrorCode::invalid, "parameters must be an object");
    for (auto it = p.begin(); it != p.end(); ++it)
      s.parameters[it.key()] = decode_scalar(it.value());
    s.environment = j.value("environment", std::map<std::string, std::string>{});
    s.labels = j.value("labels", std::map<std::string, std::string>{});
    auto r = j.value("resources", Json::object());
    s.resources = {r.value("cpu_millis", 1000), r.value("memory_mib", 512)};
    auto retry = j.value("retry", Json::object());
    s.retry = {retry.value("max_attempts", 3), retry.value("retry_exit", false),
               retry.value("retry_timeout", false)};
    s.timeout_seconds = j.value("timeout_seconds", 1800);
    s.priority = j.value("priority", 0);
    auto source = j.value("source", Json::object());
    s.source_repository = source.value("repository", "");
    s.source_revision = source.value("revision", "");
    validate(s);
    return s;
  } catch (const Json::exception &e) {
    throw Error(ErrorCode::invalid, std::string("invalid specification: ") + e.what());
  }
}
Json encode(const Run &r) {
  return {{"id", r.id},
          {"spec", encode(r.spec)},
          {"status", to_string(r.status)},
          {"generation", r.generation},
          {"active_attempt", r.active_attempt},
          {"sweep_id", r.sweep_id},
          {"parent_run_id", r.parent_run_id},
          {"created_at", r.created_at},
          {"updated_at", r.updated_at}};
}
Json encode(const Attempt &a) {
  Json j = {{"id", a.id},
            {"run_id", a.run_id},
            {"generation", a.generation},
            {"worker_id", a.worker_id},
            {"status", a.status},
            {"reason", a.reason},
            {"runtime_id", a.runtime_id},
            {"cleanup_status", a.cleanup_status},
            {"created_at", a.created_at},
            {"started_at", a.started_at},
            {"finished_at", a.finished_at},
            {"acknowledged_sequence", a.acknowledged_sequence}};
  j["exit_code"] = a.exit_code ? Json(*a.exit_code) : Json(nullptr);
  return j;
}
Json encode(const Worker &w) {
  return {
      {"id", w.id},
      {"capacity", {{"cpu_millis", w.capacity.cpu_millis}, {"memory_mib", w.capacity.memory_mib}}},
      {"reserved", {{"cpu_millis", w.reserved.cpu_millis}, {"memory_mib", w.reserved.memory_mib}}},
      {"drained", w.drained},
      {"available", w.available},
      {"heartbeat_at", w.heartbeat_at}};
}
Json encode(const Telemetry &t) {
  return {{"sequence", t.sequence},
          {"kind", t.kind},
          {"text", t.text},
          {"name", t.name},
          {"step", t.step},
          {"value", t.value},
          {"timestamp_ms", t.timestamp_ms}};
}
Telemetry decode_telemetry(const Json &j) {
  Telemetry t{j.at("sequence"),
              j.at("kind"),
              j.value("text", ""),
              j.value("name", ""),
              j.value("step", std::int64_t{}),
              j.value("value", 0.0),
              j.value("timestamp_ms", std::int64_t{})};
  validate(t);
  return t;
}
Json encode(const Artifact &a) {
  return {{"id", a.id},
          {"attempt_id", a.attempt_id},
          {"path", a.path},
          {"sha256", a.sha256},
          {"size", a.size}};
}
Json encode(const Event &e) {
  return {{"sequence", e.sequence},
          {"kind", e.kind},
          {"detail", e.detail},
          {"created_at", e.created_at}};
}
} // namespace runyard

namespace runyard {
Json encode(const SweepSpec &spec) {
  Json grid = Json::object();
  for (const auto &[key, values] : spec.grid) {
    grid[key] = Json::array();
    for (const auto &value : values)
      std::visit([&](const auto &v) { grid[key].push_back(v); }, value);
  }
  return {{"base", encode(spec.base)}, {"grid", grid}};
}
Json encode(const Sweep &s) {
  return {
      {"id", s.id}, {"spec", encode(s.spec)}, {"run_ids", s.run_ids}, {"created_at", s.created_at}};
}
SweepSpec decode_sweep(const Json &j) {
  SweepSpec spec;
  spec.base = decode_spec(j.at("base"));
  auto grid = j.at("grid");
  if (!grid.is_object())
    throw Error(ErrorCode::invalid, "grid must be an object");
  for (auto it = grid.begin(); it != grid.end(); ++it) {
    if (!it.value().is_array())
      throw Error(ErrorCode::invalid, "grid dimensions must be arrays");
    for (const auto &value : it.value())
      spec.grid[it.key()].push_back(decode_scalar(value));
    if (it.value().empty())
      throw Error(ErrorCode::invalid, "grid dimensions cannot be empty");
  }
  return spec;
}
} // namespace runyard
