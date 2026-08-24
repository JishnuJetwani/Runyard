#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <nvml.h>
#include <string>
namespace {
bool mode(const char *name) {
  auto value = std::getenv("RUNYARD_NVML_FIXTURE_MODE");
  return value && std::string(value) == name;
}
} // namespace
extern "C" {
nvmlReturn_t nvmlInit_v2() {
  return mode("init-error") ? NVML_ERROR_DRIVER_NOT_LOADED : NVML_SUCCESS;
}
nvmlReturn_t nvmlShutdown() { return NVML_SUCCESS; }
nvmlReturn_t nvmlDeviceGetCount_v2(unsigned int *count) {
  *count = 2;
  return NVML_SUCCESS;
}
nvmlReturn_t nvmlDeviceGetHandleByIndex_v2(unsigned int index, nvmlDevice_t *device) {
  if (mode("permission") && index == 1)
    return NVML_ERROR_NO_PERMISSION;
  device->handle = reinterpret_cast<nvmlDevice_st *>(
      static_cast<std::uintptr_t>(mode("reverse") ? 2 - index : index + 1));
  return NVML_SUCCESS;
}
nvmlReturn_t nvmlDeviceGetUUID(nvmlDevice_t device, char *uuid, unsigned int size) {
  std::snprintf(uuid, size, "GPU-%lu",
                static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(device.handle)));
  return NVML_SUCCESS;
}
nvmlReturn_t nvmlDeviceGetName(nvmlDevice_t, char *name, unsigned int size) {
  std::snprintf(name, size, "Fixture GPU");
  return NVML_SUCCESS;
}
nvmlReturn_t nvmlDeviceGetMemoryInfo(nvmlDevice_t, nvmlMemory_t *memory) {
  memory->total = 24ULL * 1024 * 1024 * 1024;
  return mode("query-error") ? NVML_ERROR_GPU_IS_LOST : NVML_SUCCESS;
}
nvmlReturn_t nvmlDeviceGetMigMode(nvmlDevice_t, unsigned int *current, unsigned int *pending) {
  *current = *pending = mode("mig") ? 1 : 0;
  return NVML_SUCCESS;
}
}
