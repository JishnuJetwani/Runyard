#pragma once
#include "runyard/postgres/store.hpp"
#include <cstdlib>
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
    {
      auto c = pool->acquire();
      pqxx::work tx(c.get());
      tx.exec("TRUNCATE runs,workers,sweeps CASCADE");
      tx.commit();
    }
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
