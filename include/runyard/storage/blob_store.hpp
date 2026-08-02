#pragma once
#include <filesystem>
#include <string>

namespace runyard {
class BlobStore {
public:
  virtual ~BlobStore() = default;
  virtual void put(const std::string &key, const std::filesystem::path &source) = 0;
  virtual void get(const std::string &key, const std::filesystem::path &destination) = 0;
};
class FilesystemStore final : public BlobStore {
public:
  explicit FilesystemStore(std::filesystem::path root) : root_(std::move(root)) {}
  void put(const std::string &, const std::filesystem::path &) override;
  void get(const std::string &, const std::filesystem::path &) override;

private:
  std::filesystem::path root_;
};
} // namespace runyard
