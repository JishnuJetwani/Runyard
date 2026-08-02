#include "runyard/domain/error.hpp"
#include "runyard/domain/model.hpp"
#include "runyard/storage/blob_store.hpp"
#include "runyard/support/crypto.hpp"

namespace runyard {
namespace {
void atomic_copy(const std::filesystem::path &source, const std::filesystem::path &target) {
  std::filesystem::create_directories(target.parent_path());
  auto temporary = target.string() + ".tmp-" + random_id();
  try {
    std::filesystem::copy_file(source, temporary);
    std::filesystem::rename(temporary, target);
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    throw;
  }
}
} // namespace
void FilesystemStore::put(const std::string &key, const std::filesystem::path &source) {
  validate_relative_path(key);
  atomic_copy(source, root_ / key);
}
void FilesystemStore::get(const std::string &key, const std::filesystem::path &destination) {
  validate_relative_path(key);
  if (!std::filesystem::is_regular_file(root_ / key))
    throw Error(ErrorCode::not_found, "artifact content missing");
  atomic_copy(root_ / key, destination);
}
} // namespace runyard
