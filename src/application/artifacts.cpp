#include "runyard/application/artifacts.hpp"
#include "runyard/domain/error.hpp"
#include "runyard/support/crypto.hpp"

namespace runyard {
std::filesystem::path ArtifactService::temporary_path() const {
  return root_ / "staging" / random_id();
}
Artifact ArtifactService::publish(const std::string &attempt, int generation,
                                  const std::string &instance, const std::string &relative,
                                  const std::string &expected, const std::filesystem::path &file) {
  validate_relative_path(relative);
  auto checksum = sha256_file(file.string());
  if (checksum != expected)
    throw Error(ErrorCode::invalid, "artifact checksum mismatch");
  auto size = std::filesystem::file_size(file);
  if (size > 256 * 1024 * 1024)
    throw Error(ErrorCode::exhausted, "artifact exceeds 256 MiB");
  repository_.verify_owner(attempt, generation, instance);
  Artifact artifact{sha256(attempt + ":" + relative),
                    attempt,
                    relative,
                    attempt + "/" + sha256(relative) + "-" + checksum,
                    checksum,
                    size};
  blobs_.put(artifact.storage_key, file);
  // Recheck ownership after remote storage I/O; an expired upload cannot become a result.
  return repository_.publish_artifact(artifact, generation, instance);
}
std::vector<Artifact> ArtifactService::list(const std::string &run, const std::string &attempt) {
  repository_.get_run(run);
  return repository_.artifacts(run, attempt);
}
std::filesystem::path ArtifactService::download(const std::string &id) {
  auto artifact = repository_.get_artifact(id);
  auto path = root_ / "downloads" / artifact.id;
  if (!std::filesystem::exists(path)) {
    std::filesystem::create_directories(path.parent_path());
    blobs_.get(artifact.storage_key, path);
    if (sha256_file(path.string()) != artifact.sha256) {
      std::filesystem::remove(path);
      throw Error(ErrorCode::unavailable, "stored artifact checksum mismatch");
    }
  }
  return path;
}
} // namespace runyard
