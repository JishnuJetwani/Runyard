#include "runyard/runner/client.hpp"
#include "runyard/domain/error.hpp"

namespace runyard {
void check_rpc(const grpc::Status &status) {
  if (status.ok())
    return;
  auto code = ErrorCode::unavailable;
  if (status.error_code() == grpc::StatusCode::FAILED_PRECONDITION)
    code = ErrorCode::stale;
  if (status.error_code() == grpc::StatusCode::INVALID_ARGUMENT)
    code = ErrorCode::invalid;
  if (status.error_code() == grpc::StatusCode::UNAUTHENTICATED)
    code = ErrorCode::unauthorized;
  throw Error(code, status.error_message());
}
wire::StartReply AttemptClient::start() {
  grpc::ClientContext context;
  prepare(context, token_);
  wire::StartReply reply;
  check_rpc(stub_->Start(&context, owner_, &reply));
  return reply;
}
int AttemptClient::heartbeat() {
  grpc::ClientContext context;
  prepare(context, token_, 2);
  wire::Ack reply;
  check_rpc(stub_->Heartbeat(&context, owner_, &reply));
  return reply.lease_seconds();
}
void AttemptClient::begin_finalization() {
  grpc::ClientContext context;
  prepare(context, token_);
  wire::Empty reply;
  check_rpc(stub_->BeginFinalization(&context, owner_, &reply));
}
void AttemptClient::complete(int exit_code, const std::string &reason, std::int64_t sequence) {
  grpc::ClientContext context;
  prepare(context, token_);
  wire::Completion request;
  *request.mutable_owner() = owner_;
  request.set_exit_code(exit_code);
  request.set_reason(reason);
  request.set_final_sequence(sequence);
  wire::Empty reply;
  check_rpc(stub_->Complete(&context, request, &reply));
}
} // namespace runyard
