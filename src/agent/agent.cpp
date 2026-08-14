#include "runyard/agent/agent.hpp"
#include "runyard/domain/error.hpp"
#include "runyard/runner/client.hpp"
#include "runyard/serialization/json.hpp"
#include "runyard/support/crypto.hpp"
#include <atomic>
#include <condition_variable>
#include <spdlog/spdlog.h>
#include <thread>

namespace runyard {
void run_agent(const AgentConfig &config, ExecutionBackend &backend,
               const std::function<bool()> &stop_requested) {
  auto stub = wire::AgentService::NewStub(
      make_channel(config.endpoint, config.ca_file, config.development));
  wire::WorkerIdentity identity;
  identity.set_id(config.id);
  identity.set_session(random_id());
  wire::RegisterRequest registration;
  *registration.mutable_worker() = identity;
  registration.set_cpu_millis(config.capacity.cpu_millis);
  registration.set_memory_mib(config.capacity.memory_mib);
  while (!stop_requested()) {
    grpc::ClientContext context;
    prepare(context, config.token);
    wire::Empty reply;
    auto status = stub->Register(&context, registration, &reply);
    if (status.ok())
      break;
    if (status.error_code() != grpc::StatusCode::UNAVAILABLE)
      check_rpc(status);
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  std::atomic<bool> stale{false};
  std::jthread heartbeat([&](std::stop_token stop) {
    std::mutex mutex;
    std::condition_variable_any wake;
    std::unique_lock lock(mutex);
    while (!stop.stop_requested()) {
      grpc::ClientContext context;
      prepare(context, config.token, 2);
      wire::Empty reply;
      auto status = stub->Heartbeat(&context, identity, &reply);
      if (status.error_code() == grpc::StatusCode::FAILED_PRECONDITION) {
        stale = true;
        break;
      }
      wake.wait_for(lock, stop, std::chrono::seconds(5), [] { return false; });
    }
  });
  auto report = [&](const std::string &attempt, const std::string &runtime, bool stopped) {
    wire::RuntimeReport request;
    *request.mutable_worker() = identity;
    request.set_attempt_id(attempt);
    request.set_runtime_id(runtime);
    request.set_stopped(stopped);
    grpc::ClientContext context;
    prepare(context, config.token);
    wire::Empty reply;
    check_rpc(stub->ReportRuntime(&context, request, &reply));
  };
  while (!stop_requested() && !stale) {
    try {
      wire::Inventory inventory;
      *inventory.mutable_worker() = identity;
      for (const auto &id : backend.inventory())
        inventory.add_attempts(id);
      grpc::ClientContext inventory_context;
      prepare(inventory_context, config.token);
      wire::Decisions decisions;
      check_rpc(stub->Reconcile(&inventory_context, inventory, &decisions));
      for (const auto &id : decisions.stop_attempts())
        backend.remove(id);
      grpc::ClientContext context;
      prepare(context, config.token);
      wire::WorkReply reply;
      check_rpc(stub->Poll(&context, identity, &reply));
      for (const auto &id : reply.cleanup_attempts()) {
        backend.remove(id);
        report(id, "", true);
      }
      if (reply.has_work()) {
        const auto &a = reply.assignment();
        Attempt attempt;
        attempt.id = a.attempt_id();
        attempt.run_id = a.run_id();
        attempt.generation = a.generation();
        Launch launch{{attempt, decode_spec(Json::parse(a.specification_json()))}, a.capability()};
        auto runtime = backend.ensure(launch);
        report(attempt.id, runtime, false);
        spdlog::info("{}", Json{{"event", "attempt_launched"},
                                {"attempt_id", attempt.id},
                                {"run_id", attempt.run_id},
                                {"worker_id", config.id}}
                               .dump());
      }
    } catch (const Error &e) {
      if (e.code() == ErrorCode::stale)
        throw;
      spdlog::warn("agent operation: {}", e.what());
    } catch (const std::exception &e) {
      spdlog::warn("agent operation: {}", e.what());
    }
    for (int i = 0; i < 10 && !stop_requested() && !stale; ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (stale)
    throw Error(ErrorCode::stale, "another agent replaced this session");
}
} // namespace runyard
