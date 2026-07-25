#pragma once
#include <string>
#include <string_view>

namespace runyard {
std::string random_id();
std::string sha256(std::string_view data);
std::string sha256_file(const std::string &path);
std::string sign(std::string_view secret, std::string_view message);
bool constant_equal(std::string_view left, std::string_view right);
} // namespace runyard
