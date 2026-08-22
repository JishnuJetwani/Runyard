#include "runyard/domain/error.hpp"
#include "runyard/domain/model.hpp"
#include "runyard/storage/blob_store.hpp"
#include "runyard/support/crypto.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <vector>

namespace runyard {
namespace {
void synchronize(const std::filesystem::path &path) {
  struct Descriptor {
    int value;
    ~Descriptor() {
      if (value >= 0)
        close(value);
    }
  } descriptor{open(path.c_str(), O_RDONLY | O_CLOEXEC)};
  if (descriptor.value < 0 || fsync(descriptor.value) != 0)
    throw Error(ErrorCode::unavailable, "cannot synchronize artifact storage");
}
void atomic_copy(const std::filesystem::path &source, const std::filesystem::path &target) {
  auto parent = std::filesystem::absolute(target).parent_path();
  std::vector<std::filesystem::path> missing;
  for (auto directory = parent; !std::filesystem::exists(directory);
       directory = directory.parent_path())
    missing.push_back(directory);
  std::filesystem::create_directories(parent);
  auto temporary = target.string() + ".tmp-" + random_id();
  try {
    std::filesystem::copy_file(source, temporary);
    synchronize(temporary);
    std::filesystem::rename(temporary, target);
    // Persist the rename and any newly created directory entries before SQL publication.
    synchronize(parent);
    for (const auto &directory : missing)
      synchronize(directory.parent_path());
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
