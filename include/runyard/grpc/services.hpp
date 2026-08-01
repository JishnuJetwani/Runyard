#pragma once
#include "runyard.grpc.pb.h"
#include "runyard/application/repository.hpp"
#include "runyard/support/config.hpp"
#include <grpcpp/grpcpp.h>

namespace runyard {
namespace wire = rpc::v1;
std::string attempt_capability(const std::string &secret, const std::string &id, int generation);
std::shared_ptr<grpc::Channel> make_channel(const std::string &address, const std::string &ca_file,
                                            bool development);
void prepare(grpc::ClientContext &context, const std::string &token, int timeout_seconds = 5);

class AgentRpc final : public wire::AgentService::Service {
public:
  AgentRpc(Repository &repository, const ServerConfig &config, std::function<bool()> ready)
      : repository_(repository), config_(config), ready_(std::move(ready)) {}
  grpc::Status Register(grpc::ServerContext *, const wire::RegisterRequest *,
                        wire::Empty *) override;
  grpc::Status Heartbeat(grpc::ServerContext *, const wire::WorkerIdentity *,
                         wire::Empty *) override;
  grpc::Status Poll(grpc::ServerContext *, const wire::WorkerIdentity *,
                    wire::WorkReply *) override;
  grpc::Status ReportRuntime(grpc::ServerContext *, const wire::RuntimeReport *,
                             wire::Empty *) override;

private:
  void authorize(grpc::ServerContext *);
  Repository &repository_;
  ServerConfig config_;
  std::function<bool()> ready_;
};
class AttemptRpc final : public wire::AttemptService::Service {
public:
  AttemptRpc(Repository &repository, const ServerConfig &config, std::function<bool()> ready)
      : repository_(repository), config_(config), ready_(std::move(ready)) {}
  grpc::Status Start(grpc::ServerContext *, const wire::Owner *, wire::StartReply *) override;
  grpc::Status Heartbeat(grpc::ServerContext *, const wire::Owner *, wire::Ack *) override;
  grpc::Status BeginFinalization(grpc::ServerContext *, const wire::Owner *,
                                 wire::Empty *) override;
  grpc::Status Complete(grpc::ServerContext *, const wire::Completion *, wire::Empty *) override;

private:
  void authorize(grpc::ServerContext *, const wire::Owner &);
  Repository &repository_;
  ServerConfig config_;
  std::function<bool()> ready_;
};
std::unique_ptr<grpc::Server> start_grpc(const ServerConfig &, AgentRpc &, AttemptRpc &);
} // namespace runyard
