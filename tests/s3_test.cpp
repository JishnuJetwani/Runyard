#include "runyard/storage/s3.hpp"
#include "runyard/support/config.hpp"
#include "runyard/support/crypto.hpp"
#include <fstream>
#include <gtest/gtest.h>

TEST(S3, RoundTripDuplicateUploadMissingObjectAndUnsafeKey) {
  auto endpoint = runyard::env("RUNYARD_TEST_S3_ENDPOINT");
  if (endpoint.empty())
    GTEST_SKIP() << "set RUNYARD_TEST_S3_ENDPOINT for a disposable Moto server";
  runyard::S3Store store({"runyard-test", "us-east-1", endpoint, true});
  auto root = std::filesystem::temp_directory_path() / runyard::random_id();
  std::filesystem::create_directories(root);
  {
    std::ofstream f(root / "source");
    f << std::string(2 * 1024 * 1024, 'x');
  }
  auto key = "artifacts/" + runyard::random_id();
  store.put(key, root / "source");
  store.put(key, root / "source");
  store.get(key, root / "copy");
  EXPECT_EQ(runyard::sha256_file((root / "source").string()),
            runyard::sha256_file((root / "copy").string()));
  EXPECT_ANY_THROW(store.get("missing", root / "missing"));
  EXPECT_FALSE(std::filesystem::exists(root / "missing"));
  EXPECT_ANY_THROW(store.put("../escape", root / "source"));
  EXPECT_ANY_THROW(store.put("absent-source", root / "absent"));
  std::filesystem::remove_all(root);
}
