#pragma once
#include "runyard/storage/blob_store.hpp"
#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <memory>

namespace runyard {
// The SDK lifetime encloses every client and response stream owned by this adapter.
class AwsRuntime {
public:
  AwsRuntime() { Aws::InitAPI(options_); }
  ~AwsRuntime() { Aws::ShutdownAPI(options_); }
  AwsRuntime(const AwsRuntime &) = delete;
  AwsRuntime &operator=(const AwsRuntime &) = delete;

private:
  Aws::SDKOptions options_;
};
struct S3Config {
  std::string bucket;
  std::string region{"us-east-1"};
  std::string endpoint;
  bool development{};
};
class S3Store final : public BlobStore {
public:
  explicit S3Store(const S3Config &);
  void put(const std::string &, const std::filesystem::path &) override;
  void get(const std::string &, const std::filesystem::path &) override;

private:
  AwsRuntime runtime_;
  std::unique_ptr<Aws::S3::S3Client> client_;
  std::string bucket_;
};
} // namespace runyard
