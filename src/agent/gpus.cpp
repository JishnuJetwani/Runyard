#include "gpus.hpp"
#include <condition_variable>
#include <spdlog/spdlog.h>

namespace runyard {
void report_gpus(wire::AgentService::Stub &stub, const wire::WorkerIdentity &worker,
                 const std::string &token, GpuInventory &inventory,
                 const std::atomic<bool> &reconciled, std::stop_token stop) {
  std::mutex mutex;
  std::condition_variable_any wake;
  std::unique_lock lock(mutex);
  std::int64_t sequence = 0;
  while (!stop.stop_requested()) {
    if (!reconciled) {
      wake.wait_for(lock, stop, std::chrono::seconds(1), [] { return false; });
      continue;
    }
    wire::GpuInventory request;
    *request.mutable_worker() = worker;
    request.set_sequence(++sequence);
    try {
      for (const auto &device : inventory.discover()) {
        auto *record = request.add_devices();
        record->set_uuid(device.uuid);
        record->set_name(device.name);
        record->set_memory_mib(device.memory_mib);
        record->set_eligible(device.eligible);
        record->set_reason(device.reason);
      }
      request.set_ready(true);
    } catch (const std::exception &e) {
      spdlog::warn("GPU discovery: {}", e.what());
    }
    grpc::ClientContext context;
    prepare(context, token, 2);
    wire::Empty reply;
    auto status = stub.ReportGpuInventory(&context, request, &reply);
    if (!status.ok())
      spdlog::warn("GPU inventory report: {}", status.error_message());
    wake.wait_for(lock, stop, std::chrono::seconds(10), [] { return false; });
  }
}
} // namespace runyard
