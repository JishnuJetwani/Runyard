#include "runyard/domain/error.hpp"
#include "runyard/storage/blob_store.hpp"
#include "runyard/support/crypto.hpp"
#include <fstream>
#include <gtest/gtest.h>

TEST(Storage, FilesystemObjectsAreCompleteAndKeysCannotEscape) {
  auto root = std::filesystem::temp_directory_path() / runyard::random_id();
  std::filesystem::create_directories(root);
  std::ofstream(root / "source") << "payload";
  runyard::FilesystemStore store(root / "objects");
  store.put("attempt/hash", root / "source");
  store.get("attempt/hash", root / "copy");
  EXPECT_EQ(runyard::sha256_file((root / "copy").string()), runyard::sha256("payload"));
  EXPECT_THROW(store.put("../escape", root / "source"), runyard::Error);
  EXPECT_THROW(store.get("missing", root / "bad"), runyard::Error);
  EXPECT_FALSE(std::filesystem::exists(root / "bad"));
  std::filesystem::remove_all(root);
}
