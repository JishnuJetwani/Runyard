#include "runyard/nvidia/inventory.hpp"
#include "runyard/domain/error.hpp"
#include <algorithm>
#include <dlfcn.h>
#include <nvml.h>
#include <sstream>

namespace runyard {
namespace {
class Library {
public:
  explicit Library(const std::string &path) : handle_(dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL)) {
    if (!handle_)
      throw Error(ErrorCode::unavailable, "NVML library unavailable");
  }
  ~Library() { dlclose(handle_); }
  Library(const Library &) = delete;
  Library &operator=(const Library &) = delete;
  template <typename Function> Function symbol(const char *name) const {
    auto address = dlsym(handle_, name);
    if (!address)
      throw Error(ErrorCode::unavailable, std::string("NVML entrypoint unavailable: ") + name);
    return reinterpret_cast<Function>(address);
  }

private:
  void *handle_;
};
class Session {
public:
  explicit Session(const Library &library)
      : shutdown_(library.symbol<decltype(&nvmlShutdown)>("nvmlShutdown")) {
    if (library.symbol<decltype(&nvmlInit_v2)>("nvmlInit_v2")() != NVML_SUCCESS)
      throw Error(ErrorCode::unavailable, "NVML initialization failed");
  }
  ~Session() { shutdown_(); }
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;

private:
  decltype(&nvmlShutdown) shutdown_;
};
} // namespace

std::set<std::string> gpu_allowlist(const std::string &value) {
  std::set<std::string> result;
  if (value.empty())
    return result;
  std::istringstream stream(value);
  std::string id;
  while (std::getline(stream, id, ',')) {
    if (!id.starts_with("GPU-") || id.size() > 100 ||
        id.find_first_of(" \t\n") != std::string::npos || !result.insert(id).second)
      throw Error(ErrorCode::invalid, "GPU allowlist requires distinct comma-separated UUIDs");
  }
  if (value.back() == ',' || result.size() > 64)
    throw Error(ErrorCode::invalid, "invalid GPU allowlist");
  return result;
}

std::vector<GpuDevice> NvidiaInventory::discover() {
  Library library(library_);
  Session session(library);
  auto count_devices = library.symbol<decltype(&nvmlDeviceGetCount_v2)>("nvmlDeviceGetCount_v2");
  auto device_at =
      library.symbol<decltype(&nvmlDeviceGetHandleByIndex_v2)>("nvmlDeviceGetHandleByIndex_v2");
  auto uuid_of = library.symbol<decltype(&nvmlDeviceGetUUID)>("nvmlDeviceGetUUID");
  auto name_of = library.symbol<decltype(&nvmlDeviceGetName)>("nvmlDeviceGetName");
  auto memory_of = library.symbol<decltype(&nvmlDeviceGetMemoryInfo)>("nvmlDeviceGetMemoryInfo");
  auto mig_mode = library.symbol<decltype(&nvmlDeviceGetMigMode)>("nvmlDeviceGetMigMode");
  unsigned int count = 0;
  if (count_devices(&count) != NVML_SUCCESS || count > 64)
    throw Error(ErrorCode::unavailable, "NVML device enumeration failed");
  std::vector<GpuDevice> result;
  for (unsigned int index = 0; index < count; ++index) {
    nvmlDevice_t handle{};
    char uuid[NVML_DEVICE_UUID_BUFFER_SIZE]{}, name[NVML_DEVICE_NAME_BUFFER_SIZE]{};
    if (device_at(index, &handle) != NVML_SUCCESS ||
        uuid_of(handle, uuid, sizeof(uuid)) != NVML_SUCCESS)
      continue;
    if (!std::string(uuid).starts_with("GPU-") || (!allowed_.empty() && !allowed_.contains(uuid)))
      continue;
    GpuDevice device{uuid, "", 0, true, ""};
    nvmlMemory_t memory{};
    unsigned int current = 0, pending = 0;
    auto mig = mig_mode(handle, &current, &pending);
    if (name_of(handle, name, sizeof(name)) != NVML_SUCCESS ||
        memory_of(handle, &memory) != NVML_SUCCESS) {
      device.eligible = false;
      device.reason = "device metadata unavailable";
    } else if ((mig != NVML_SUCCESS && mig != NVML_ERROR_NOT_SUPPORTED) ||
               (mig == NVML_SUCCESS && (current != 0 || pending != 0))) {
      device.eligible = false;
      device.reason = "whole-device mode unavailable";
    }
    device.name = name;
    device.memory_mib = memory.total / (1024 * 1024);
    result.push_back(std::move(device));
  }
  // UUID order is stable even when the driver changes enumeration indices after reboot.
  std::sort(result.begin(), result.end(),
            [](const auto &a, const auto &b) { return a.uuid < b.uuid; });
  for (const auto &id : allowed_)
    if (std::none_of(result.begin(), result.end(), [&](const auto &d) { return d.uuid == id; }))
      throw Error(ErrorCode::unavailable, "allowlisted GPU was not discovered: " + id);
  return result;
}
} // namespace runyard
