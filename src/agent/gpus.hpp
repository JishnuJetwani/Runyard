#pragma once
#include "runyard/grpc/services.hpp"
#include "runyard/nvidia/inventory.hpp"
#include <atomic>
#include <stop_token>

namespace runyard {
void report_gpus(wire::AgentService::Stub &, const wire::WorkerIdentity &, const std::string &token,
                 GpuInventory &, const std::atomic<bool> &reconciled, std::stop_token);
}
