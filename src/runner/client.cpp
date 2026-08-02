#include "runyard/runner/client.hpp"
#include "runyard/domain/error.hpp"
#include "runyard/support/crypto.hpp"
#include <array>
#include <fstream>

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
std::int64_t AttemptClient::report(const std::vector<Telemetry> &records) {
  wire::TelemetryBatch batch;
  *batch.mutable_owner() = owner_;
  for (const auto &r : records) {
    auto *t = batch.add_records();
    t->set_sequence(r.sequence);
    t->set_kind(r.kind);
    t->set_text(r.text);
    t->set_name(r.name);
    t->set_step(r.step);
    t->set_value(r.value);
    t->set_timestamp_ms(r.timestamp_ms);
  }
  grpc::ClientContext context;
  prepare(context, token_, 2);
  wire::Ack reply;
  check_rpc(stub_->Report(&context, batch, &reply));
  return reply.sequence();
}
void AttemptClient::upload(const std::string &file, const std::string &relative) {
  auto hash = sha256_file(file);
  grpc::ClientContext context;
  prepare(context, token_, 300);
  wire::ArtifactReply reply;
  auto writer = stub_->Upload(&context, &reply);
  wire::ArtifactChunk chunk;
  *chunk.mutable_owner() = owner_;
  chunk.set_path(relative);
  chunk.set_sha256(hash);
  bool writable = writer->Write(chunk);
  std::ifstream input(file, std::ios::binary);
  std::array<char, 65536> bytes{};
  while (writable && input) {
    input.read(bytes.data(), bytes.size());
    if (input.gcount()) {
      chunk.Clear();
      chunk.set_data(bytes.data(), static_cast<std::size_t>(input.gcount()));
      writable = writer->Write(chunk);
    }
  }
  writer->WritesDone();
  check_rpc(writer->Finish());
  if (!input.eof() || reply.sha256() != hash)
    throw Error(ErrorCode::unavailable, "artifact transfer incomplete");
}
} // namespace runyard
