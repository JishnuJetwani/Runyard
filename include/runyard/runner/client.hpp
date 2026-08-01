#pragma once
#include "runyard/grpc/services.hpp"

namespace runyard {
class AttemptClient {
public:
  AttemptClient(std::shared_ptr<grpc::Channel> channel, wire::Owner owner, std::string token)
      : stub_(wire::AttemptService::NewStub(std::move(channel))), owner_(std::move(owner)),
        token_(std::move(token)) {}
  wire::StartReply start();
  int heartbeat();
  std::int64_t report(const std::vector<Telemetry> &records);
  void begin_finalization();
  void complete(int exit_code, const std::string &reason, std::int64_t final_sequence);

private:
  std::unique_ptr<wire::AttemptService::Stub> stub_;
  wire::Owner owner_;
  std::string token_;
};
void check_rpc(const grpc::Status &status);
} // namespace runyard
