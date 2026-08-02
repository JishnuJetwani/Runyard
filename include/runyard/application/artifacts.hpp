#pragma once
#include "runyard/application/repository.hpp"
#include "runyard/storage/blob_store.hpp"

namespace runyard {
class ArtifactService {
public:
  ArtifactService(Repository &repository, BlobStore &blobs, std::filesystem::path root)
      : repository_(repository), blobs_(blobs), root_(std::move(root)) {
    std::filesystem::create_directories(root_ / "staging");
  }
  std::filesystem::path temporary_path() const;
  Artifact publish(const std::string &attempt, int generation, const std::string &instance,
                   const std::string &relative, const std::string &expected_hash,
                   const std::filesystem::path &temporary);
  std::vector<Artifact> list(const std::string &run, const std::string &attempt);
  std::filesystem::path download(const std::string &id);

private:
  Repository &repository_;
  BlobStore &blobs_;
  std::filesystem::path root_;
};
} // namespace runyard
