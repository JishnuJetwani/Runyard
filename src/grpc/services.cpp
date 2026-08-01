#include "runyard/grpc/services.hpp"
#include "runyard/domain/error.hpp"
#include "runyard/serialization/json.hpp"
#include "runyard/support/crypto.hpp"
#include <spdlog/spdlog.h>

namespace runyard {
namespace {
template <class F> grpc::Status guard(F &&work) {
  try {
    work();
    return grpc::Status::OK;
  } catch (const Error &e) {
    auto code = grpc::StatusCode::UNAVAILABLE;
    switch (e.code()) {
    case ErrorCode::invalid:
      code = grpc::StatusCode::INVALID_ARGUMENT;
      break;
    case ErrorCode::unauthorized:
      code = grpc::StatusCode::UNAUTHENTICATED;
      break;
    case ErrorCode::not_found:
      code = grpc::StatusCode::NOT_FOUND;
      break;
    case ErrorCode::stale:
    case ErrorCode::conflict:
      code = grpc::StatusCode::FAILED_PRECONDITION;
      break;
    case ErrorCode::exhausted:
      code = grpc::StatusCode::RESOURCE_EXHAUSTED;
      break;
    default:
      break;
    }
    return {code, e.what()};
  } catch (const std::exception &e) {
    spdlog::error("RPC failed: {}", e.what());
    return {grpc::StatusCode::UNAVAILABLE, "operation temporarily unavailable"};
  }
}
void authenticate(grpc::ServerContext *context, const std::string &token, bool ready) {
  auto found = context->client_metadata().find("authorization");
  if (token.empty() || found == context->client_metadata().end() ||
      !constant_equal(std::string(found->second.data(), found->second.length()), "Bearer " + token))
    throw Error(ErrorCode::unauthorized, "invalid RPC credentials");
  if (!ready)
    throw Error(ErrorCode::unavailable, "coordinator is not ready");
  if (context->IsCancelled())
    throw Error(ErrorCode::unavailable, "RPC cancelled");
}
} // namespace
std::string attempt_capability(const std::string &secret, const std::string &id, int generation) {
  return sign(secret, "attempt:" + id + ":" + std::to_string(generation));
}
void prepare(grpc::ClientContext &context, const std::string &token, int timeout_seconds) {
  context.AddMetadata("authorization", "Bearer " + token);
  context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(timeout_seconds));
}
std::shared_ptr<grpc::Channel> make_channel(const std::string &address, const std::string &ca_file,
                                            bool development) {
  std::shared_ptr<grpc::ChannelCredentials> credentials;
  if (development && ca_file.empty())
    credentials = grpc::InsecureChannelCredentials();
  else {
    grpc::SslCredentialsOptions options;
    if (!ca_file.empty())
      options.pem_root_certs = read_file(ca_file);
    credentials = grpc::SslCredentials(options);
  }
  return grpc::CreateChannel(address, credentials);
}
void AgentRpc::authorize(grpc::ServerContext *context) {
  authenticate(context, config_.worker_token, ready_());
  if (config_.mode != "docker")
    throw Error(ErrorCode::invalid, "Docker agents are disabled in Kubernetes mode");
}
grpc::Status AgentRpc::Register(grpc::ServerContext *c, const wire::RegisterRequest *r,
                                wire::Empty *) {
  return guard([&] {
    authorize(c);
    repository_.register_worker(r->worker().id(), r->worker().session(),
                                {r->cpu_millis(), r->memory_mib()});
  });
}
grpc::Status AgentRpc::Heartbeat(grpc::ServerContext *c, const wire::WorkerIdentity *r,
                                 wire::Empty *) {
  return guard([&] {
    authorize(c);
    repository_.worker_heartbeat(r->id(), r->session());
  });
}
grpc::Status AgentRpc::Poll(grpc::ServerContext *c, const wire::WorkerIdentity *r,
                            wire::WorkReply *reply) {
  return guard([&] {
    authorize(c);
    for (const auto &a : repository_.cleanup(r->id(), r->session()))
      reply->add_cleanup_attempts(a.id);
    auto assignment = repository_.assign(r->id(), r->session());
    if (!assignment)
      return;
    reply->set_has_work(true);
    auto *a = reply->mutable_assignment();
    a->set_attempt_id(assignment->attempt.id);
    a->set_run_id(assignment->attempt.run_id);
    a->set_generation(assignment->attempt.generation);
    a->set_specification_json(encode(assignment->spec).dump());
    a->set_capability(attempt_capability(config_.signing_key, a->attempt_id(), a->generation()));
  });
}
grpc::Status AgentRpc::ReportRuntime(grpc::ServerContext *c, const wire::RuntimeReport *r,
                                     wire::Empty *) {
  return guard([&] {
    authorize(c);
    repository_.runtime_report(r->worker().id(), r->worker().session(), r->attempt_id(),
                               r->runtime_id(), r->stopped());
  });
}
void AttemptRpc::authorize(grpc::ServerContext *c, const wire::Owner &owner) {
  authenticate(c, attempt_capability(config_.signing_key, owner.attempt_id(), owner.generation()),
               ready_());
}
grpc::Status AttemptRpc::Start(grpc::ServerContext *c, const wire::Owner *r,
                               wire::StartReply *reply) {
  return guard([&] {
    authorize(c, *r);
    auto a = repository_.start(r->attempt_id(), r->generation(), r->instance_id());
    reply->set_specification_json(encode(a.spec).dump());
    reply->set_lease_seconds(config_.timing.lease_seconds);
    reply->set_heartbeat_seconds(config_.timing.heartbeat_seconds);
    reply->set_termination_seconds(config_.timing.termination_seconds);
    reply->set_finalization_seconds(config_.timing.finalization_seconds);
  });
}
grpc::Status AttemptRpc::Heartbeat(grpc::ServerContext *c, const wire::Owner *r, wire::Ack *reply) {
  return guard([&] {
    authorize(c, *r);
    repository_.heartbeat(r->attempt_id(), r->generation(), r->instance_id());
    reply->set_lease_seconds(config_.timing.lease_seconds);
  });
}
grpc::Status AttemptRpc::BeginFinalization(grpc::ServerContext *c, const wire::Owner *r,
                                           wire::Empty *) {
  return guard([&] {
    authorize(c, *r);
    repository_.begin_finalization(r->attempt_id(), r->generation(), r->instance_id());
  });
}
grpc::Status AttemptRpc::Complete(grpc::ServerContext *c, const wire::Completion *r,
                                  wire::Empty *) {
  return guard([&] {
    authorize(c, r->owner());
    repository_.finish(r->owner().attempt_id(), r->owner().generation(), r->owner().instance_id(),
                       r->exit_code(), r->reason(), r->final_sequence());
  });
}
grpc::Status AttemptRpc::Report(grpc::ServerContext *c, const wire::TelemetryBatch *r,
                                wire::Ack *reply) {
  return guard([&] {
    authorize(c, r->owner());
    std::vector<Telemetry> records;
    for (const auto &t : r->records())
      records.push_back(
          {t.sequence(), t.kind(), t.text(), t.name(), t.step(), t.value(), t.timestamp_ms()});
    reply->set_sequence(repository_.report(r->owner().attempt_id(), r->owner().generation(),
                                           r->owner().instance_id(), records));
  });
}
std::unique_ptr<grpc::Server> start_grpc(const ServerConfig &config, AgentRpc &agent,
                                         AttemptRpc &attempt) {
  grpc::ServerBuilder builder;
  std::shared_ptr<grpc::ServerCredentials> credentials;
  if (config.development && config.certificate.empty())
    credentials = grpc::InsecureServerCredentials();
  else {
    grpc::SslServerCredentialsOptions options;
    options.pem_key_cert_pairs.push_back(
        {read_file(config.private_key), read_file(config.certificate)});
    credentials = grpc::SslServerCredentials(options);
  }
  builder.AddListeningPort(config.host + ":" + std::to_string(config.grpc_port), credentials);
  builder.RegisterService(&agent);
  builder.RegisterService(&attempt);
  builder.SetMaxReceiveMessageSize(1024 * 1024);
  grpc::ResourceQuota quota;
  quota.SetMaxThreads(32);
  builder.SetResourceQuota(quota);
  auto server = builder.BuildAndStart();
  if (!server)
    throw Error(ErrorCode::unavailable, "cannot start gRPC listener");
  return server;
}
} // namespace runyard
