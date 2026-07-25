#include "runyard/domain/error.hpp"
#include "runyard/postgres/store.hpp"
#include "runyard/serialization/json.hpp"
#include "runyard/support/crypto.hpp"
#include <cstdlib>
#include <future>
#include <gtest/gtest.h>

using namespace runyard;
class Database : public ::testing::Test {
protected:
  std::unique_ptr<ConnectionPool> pool;
  std::unique_ptr<PostgresStore> store;
  void SetUp() override {
    auto dsn = std::getenv("RUNYARD_TEST_DATABASE");
    if (!dsn)
      GTEST_SKIP() << "RUNYARD_TEST_DATABASE not set";
    pool = std::make_unique<ConnectionPool>(dsn, 4);
    migrate(*pool, RUNYARD_MIGRATIONS);
    store = std::make_unique<PostgresStore>(*pool);
  }
  RunSpec spec() {
    RunSpec s;
    s.name = "db-test";
    s.image = "fixture@sha256:" + std::string(64, 'a');
    s.command = {"true"};
    return s;
  }
};
TEST_F(Database, SubmissionSurvivesNewRepository) {
  auto s = spec();
  auto run = store->submit(s, random_id(), sha256(encode(s).dump()));
  PostgresStore second(*pool);
  EXPECT_EQ(second.get_run(run.id).spec.image, s.image);
  EXPECT_EQ(second.events(run.id, 0, 10).size(), 1);
}
TEST_F(Database, ConcurrentSubmissionIsIdempotent) {
  auto s = spec();
  auto key = random_id();
  auto hash = sha256(encode(s).dump());
  auto a = std::async(std::launch::async, [&] { return store->submit(s, key, hash); });
  auto b = std::async(std::launch::async, [&] { return store->submit(s, key, hash); });
  EXPECT_EQ(a.get().id, b.get().id);
  EXPECT_THROW(store->submit(s, key, "different"), Error);
}
TEST_F(Database, MissingRunIsNotFound) { EXPECT_THROW(store->get_run(random_id()), Error); }
