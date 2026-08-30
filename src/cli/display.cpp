#include "runyard/cli/client.hpp"
#include <iostream>

namespace runyard {
void print_result(const Json &value, bool json) {
  if (json) {
    std::cout << value.dump(2) << '\n';
    return;
  }
  if (value.contains("backend") && value.contains("gpu")) {
    const auto &g = value.at("gpu");
    std::cout << value.at("backend").get<std::string>() << " GPU capacity: " << g.at("capacity")
              << "  reserved: " << g.at("reserved") << "  available: "
              << (g.at("available_estimate").is_null() ? "unknown"
                                                       : g.at("available_estimate").dump())
              << "  pending: " << g.at("pending") << '\n';
    if (!g.at("fresh").get<bool>())
      std::cout << "Inventory is stale or unavailable.\n";
    for (const auto &worker : value.at("workers"))
      print_result(worker, false);
    for (const auto &node : value.at("nodes"))
      std::cout << node.at("name").get<std::string>() << "  GPUs=" << node.at("allocatable")
                << "  reserved=" << node.at("reserved") << "  available="
                << (node.at("available_estimate").is_null() ? "unknown"
                                                            : node.at("available_estimate").dump())
                << '\n';
    if (!value.at("next_cursor").get<std::string>().empty())
      std::cout << "Next cursor: " << value.at("next_cursor").get<std::string>() << '\n';
    return;
  }
  if (value.contains("gpu_inventory")) {
    std::cout << value.at("id").get<std::string>() << "  CPU=" << value["capacity"]["cpu_millis"]
              << "m  memory=" << value["capacity"]["memory_mib"]
              << "MiB  GPUs=" << value["capacity"]["gpu_count"]
              << "  reserved=" << value["reserved"]["gpu_count"] << "  drained=" << value["drained"]
              << '\n';
    return;
  }
  if (value.contains("items")) {
    for (const auto &item : value["items"])
      print_result(item, false);
    if (value["items"].empty())
      std::cout << "No items.\n";
    if (value.contains("next_cursor") && value["next_cursor"].is_string() &&
        !value["next_cursor"].get<std::string>().empty())
      std::cout << "Next cursor: " << value["next_cursor"].get<std::string>() << '\n';
    return;
  }
  if (value.contains("id") && value.contains("status")) {
    std::cout << value["id"].get<std::string>() << "  " << value["status"].get<std::string>();
    if (value.contains("spec"))
      std::cout << "  " << value["spec"]["name"].get<std::string>()
                << "  GPUs=" << value["spec"]["resources"].value("gpu_count", 0);
    if (value.contains("gpu_count"))
      std::cout << "  GPUs=" << value["gpu_count"];
    if (value.contains("gpu_allocations"))
      for (const auto &allocation : value["gpu_allocations"])
        std::cout << "  " << allocation["device"]["uuid"].get<std::string>()
                  << (allocation["released_at"].get<std::string>().empty() ? " (reserved)"
                                                                           : " (released)");
    if (value.contains("node_name") && !value["node_name"].get<std::string>().empty())
      std::cout << "  node=" << value["node_name"].get<std::string>();
    std::cout << '\n';
  } else
    std::cout << value.dump(2) << '\n';
}
} // namespace runyard
