#include "runyard/storage/s3.hpp"
#include "runyard/domain/error.hpp"
#include "runyard/domain/model.hpp"
#include "runyard/support/crypto.hpp"
#include <aws/core/auth/signer/AWSAuthV4Signer.h>
#include <aws/core/utils/memory/stl/AWSStreamFwd.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <fstream>

namespace runyard {
S3Store::S3Store(const S3Config &config) : bucket_(config.bucket) {
  if (bucket_.empty())
    throw Error(ErrorCode::invalid, "S3 bucket is required");
  if (!config.development && config.endpoint.starts_with("http://"))
    throw Error(ErrorCode::invalid, "plaintext S3 endpoints require development profile");
  Aws::Client::ClientConfiguration options;
  options.region = config.region;
  options.connectTimeoutMs = 3000;
  options.requestTimeoutMs = 30000;
  options.maxConnections = 8;
  if (!config.endpoint.empty())
    options.endpointOverride = config.endpoint;
  client_ = std::make_unique<Aws::S3::S3Client>(
      options, Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never, config.endpoint.empty());
}
void S3Store::put(const std::string &key, const std::filesystem::path &source) {
  validate_relative_path(key);
  auto body = Aws::MakeShared<Aws::FStream>("runyard", source.string().c_str(),
                                            std::ios::in | std::ios::binary);
  if (!*body)
    throw Error(ErrorCode::invalid, "cannot open artifact source");
  Aws::S3::Model::PutObjectRequest request;
  request.SetBucket(bucket_);
  request.SetKey(key);
  request.SetBody(body);
  request.SetContentLength(static_cast<long long>(std::filesystem::file_size(source)));
  request.SetContentType("application/octet-stream");
  auto outcome = client_->PutObject(request);
  if (!outcome.IsSuccess())
    throw Error(ErrorCode::unavailable,
                "S3 upload failed: " + std::string(outcome.GetError().GetExceptionName()));
}
void S3Store::get(const std::string &key, const std::filesystem::path &destination) {
  validate_relative_path(key);
  if (!destination.parent_path().empty())
    std::filesystem::create_directories(destination.parent_path());
  auto temporary = destination.string() + ".partial-" + random_id();
  try {
    Aws::S3::Model::GetObjectRequest request;
    request.SetBucket(bucket_);
    request.SetKey(key);
    request.SetResponseStreamFactory([temporary] {
      return Aws::New<Aws::FStream>("runyard", temporary.c_str(),
                                    std::ios::out | std::ios::binary | std::ios::trunc);
    });
    {
      auto outcome = client_->GetObject(request);
      if (!outcome.IsSuccess())
        throw Error(ErrorCode::unavailable,
                    "S3 download failed: " + std::string(outcome.GetError().GetExceptionName()));
      auto &body = outcome.GetResult().GetBody();
      body.flush();
      if (!body)
        throw Error(ErrorCode::unavailable, "S3 download write failed");
    }
    std::filesystem::rename(temporary, destination);
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    throw;
  }
}
} // namespace runyard
