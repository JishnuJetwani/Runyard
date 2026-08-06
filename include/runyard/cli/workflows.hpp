#pragma once
#include "runyard/cli/client.hpp"
namespace runyard {
void show_logs(const Client &, const std::string &run, std::string attempt, bool follow, bool json);
void export_metrics(const Client &, const std::string &run, const std::string &attempt,
                    const std::string &name, const std::string &format);
void download_artifact(const Client &, const std::string &id, const std::string &destination);
} // namespace runyard
