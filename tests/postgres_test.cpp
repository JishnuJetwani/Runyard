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
    {
      auto c = pool->acquire();
      pqxx::work tx(c.get());
      tx.exec("TRUNCATE runs,workers CASCADE");
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

TEST_F(Database, ReservationsPreventOversubscriptionAndReplayLaunch) {
  auto s = spec();
  store->submit(s, random_id(), "one");
  store->submit(s, random_id(), "two");
  store->register_worker("worker", "session", {1000, 512});
  auto first = store->assign("worker", "session");
  ASSERT_TRUE(first);
  EXPECT_EQ(store->assign("worker", "session")->attempt.id, first->attempt.id);
  store->runtime_report("worker", "session", first->attempt.id, "container", false);
  EXPECT_FALSE(store->assign("worker", "session"));
}
TEST_F(Database, OnlyOneRunnerCanClaimAndExpiredLeaseCannotRevive) {
  auto s = spec();
  store->submit(s, random_id(), "one");
  store->register_worker("w", "s", {1000, 512});
  auto a = store->assign("w", "s")->attempt;
  store->start(a.id, a.generation, "instance-a");
  EXPECT_NO_THROW(store->start(a.id, a.generation, "instance-a"));
  EXPECT_THROW(store->start(a.id, a.generation, "instance-b"), Error);
  {
    auto c = pool->acquire();
    pqxx::work tx(c.get());
    tx.exec("UPDATE attempts SET lease_until=clock_timestamp()-interval '1 second' WHERE id=$1",
            pqxx::params{a.id});
    tx.commit();
  }
  EXPECT_THROW(store->heartbeat(a.id, a.generation, "instance-a"), Error);
  EXPECT_THROW(store->begin_finalization(a.id, a.generation, "instance-a"), Error);
}
TEST_F(Database, CompletionIsIdempotentButCannotChangeOutcome) {
  auto s = spec();
  auto run = store->submit(s, random_id(), "one");
  store->register_worker("w", "s", {1000, 512});
  auto a = store->assign("w", "s")->attempt;
  store->start(a.id, a.generation, "i");
  EXPECT_THROW(store->finish(a.id, a.generation, "i", 0, "", 0), Error);
  store->begin_finalization(a.id, a.generation, "i");
  store->finish(a.id, a.generation, "i", 0, "", 0);
  EXPECT_NO_THROW(store->finish(a.id, a.generation, "i", 0, "", 0));
  EXPECT_THROW(store->finish(a.id, a.generation, "i", 1, "", 0), Error);
  EXPECT_EQ(store->get_run(run.id).status, RunStatus::succeeded);
}
TEST_F(Database, OldAgentSessionCannotAssign) {
  store->register_worker("w", "old", {1000, 512});
  store->register_worker("w", "new", {1000, 512});
  EXPECT_THROW(store->assign("w", "old"), Error);
}

TEST_F(Database, TelemetryIsContiguousAndIdempotent) {
  auto run = store->submit(spec(), random_id(), "one");
  store->register_worker("w", "s", {1000, 512});
  auto a = store->assign("w", "s")->attempt;
  store->start(a.id, a.generation, "i");
  Telemetry first{1, "stdout", "hello", "", 0, 0, 1}, second{2, "metric", "", "score", 0, 0.5, 2};
  EXPECT_EQ(store->report(a.id, a.generation, "i", {first, second}), 2);
  EXPECT_EQ(store->report(a.id, a.generation, "i", {first, second}), 2);
  auto gap = first;
  gap.sequence = 4;
  EXPECT_THROW(store->report(a.id, a.generation, "i", {gap}), Error);
  EXPECT_EQ(store->telemetry(run.id, "", 0, 100, "logs", "").size(), 1);
  EXPECT_EQ(store->telemetry(run.id, "", 0, 100, "metric", "score").size(), 1);
  store->begin_finalization(a.id, a.generation, "i");
  EXPECT_THROW(store->finish(a.id, a.generation, "i", 0, "", 1), Error);
  EXPECT_NO_THROW(store->finish(a.id, a.generation, "i", 0, "", 2));
}

TEST_F(Database, ArtifactsArePublishedOnlyByTheLiveFinalizingAttempt) {
  auto run = store->submit(spec(), random_id(), "one");
  store->register_worker("w", "s", {1000, 512});
  auto a = store->assign("w", "s")->attempt;
  store->start(a.id, a.generation, "i");
  Artifact artifact{random_id(), a.id, "result.json", "key", std::string(64, 'a'), 10};
  EXPECT_THROW(store->publish_artifact(artifact, a.generation, "i"), Error);
  store->begin_finalization(a.id, a.generation, "i");
  EXPECT_EQ(store->publish_artifact(artifact, a.generation, "i").id, artifact.id);
  EXPECT_EQ(store->publish_artifact(artifact, a.generation, "i").id, artifact.id);
  EXPECT_EQ(store->artifacts(run.id, "").size(), 1);
  auto changed = artifact;
  changed.sha256 = std::string(64, 'b');
  EXPECT_THROW(store->publish_artifact(changed, a.generation, "i"), Error);
  store->finish(a.id, a.generation, "i", 0, "", 0);
  EXPECT_THROW(store->publish_artifact(artifact, a.generation, "i"), Error);
}

TEST_F(Database, ExpiredAttemptRetriesWithNewIdentityAndRejectsOldWrites) {
  auto run = store->submit(spec(), random_id(), "one");
  store->register_worker("w1", "s", {1000, 512});
  store->register_worker("w2", "s", {1000, 512});
  auto first = store->assign("w1", "s")->attempt;
  store->start(first.id, first.generation, "old");
  {
    auto c = pool->acquire();
    pqxx::work tx(c.get());
    tx.exec("UPDATE attempts SET lease_until=clock_timestamp()-interval '1 second' WHERE id=$1",
            pqxx::params{first.id});
    tx.commit();
  }
  store->recover();
  EXPECT_EQ(store->get_run(run.id).status, RunStatus::retry_wait);
  EXPECT_THROW(store->heartbeat(first.id, first.generation, "old"), Error);
  {
    auto c = pool->acquire();
    pqxx::work tx(c.get());
    tx.exec("UPDATE runs SET available_at=clock_timestamp() WHERE id=$1", pqxx::params{run.id});
    tx.commit();
  }
  store->recover();
  auto next = store->assign("w2", "s");
  ASSERT_TRUE(next);
  EXPECT_NE(next->attempt.id, first.id);
  EXPECT_EQ(next->attempt.generation, 2);
  EXPECT_THROW(store->start(first.id, first.generation, "old"), Error);
}
TEST_F(Database, ApplicationFailureRetriesOnlyWhenRequested) {
  auto s = spec();
  s.retry.retry_exit = true;
  auto run = store->submit(s, random_id(), "one");
  store->register_worker("w", "s", {1000, 512});
  auto a = store->assign("w", "s")->attempt;
  store->start(a.id, a.generation, "i");
  store->finish(a.id, a.generation, "i", 7, "EXIT_ERROR", 0);
  EXPECT_EQ(store->get_run(run.id).status, RunStatus::retry_wait);
  EXPECT_THROW(store->heartbeat(a.id, a.generation, "i"), Error);
}

TEST_F(Database, CancellationFencesResultsAndRetainsPendingCleanup) {
  auto run = store->submit(spec(), random_id(), "one");
  store->register_worker("w", "s", {1000, 512});
  auto a = store->assign("w", "s")->attempt;
  store->start(a.id, a.generation, "i");
  EXPECT_EQ(store->cancel(run.id).status, RunStatus::cancelled);
  EXPECT_EQ(store->cancel(run.id).status, RunStatus::cancelled);
  EXPECT_THROW(store->heartbeat(a.id, a.generation, "i"), Error);
  EXPECT_THROW(store->finish(a.id, a.generation, "i", 0, "", 0), Error);
  ASSERT_EQ(store->cleanup("w", "s").size(), 1);
  EXPECT_EQ(store->attempts(run.id)[0].cleanup_status, "PENDING");
  store->runtime_report("w", "s", a.id, "", true);
  EXPECT_EQ(store->attempts(run.id)[0].cleanup_status, "DONE");
}
TEST_F(Database, CancellationCannotOverwriteCommittedSuccess) {
  auto run = store->submit(spec(), random_id(), "one");
  store->register_worker("w", "s", {1000, 512});
  auto a = store->assign("w", "s")->attempt;
  store->start(a.id, a.generation, "i");
  store->begin_finalization(a.id, a.generation, "i");
  store->finish(a.id, a.generation, "i", 0, "", 0);
  EXPECT_EQ(store->cancel(run.id).status, RunStatus::succeeded);
}
TEST_F(Database, ExecutionTimeoutCannotBeExtendedByHeartbeat) {
  auto run = store->submit(spec(), random_id(), "one");
  store->register_worker("w", "s", {1000, 512});
  auto a = store->assign("w", "s")->attempt;
  store->start(a.id, a.generation, "i");
  {
    auto c = pool->acquire();
    pqxx::work tx(c.get());
    tx.exec(
        "UPDATE attempts SET execution_deadline=clock_timestamp()-interval '1 second' WHERE id=$1",
        pqxx::params{a.id});
    tx.commit();
  }
  EXPECT_THROW(store->heartbeat(a.id, a.generation, "i"), Error);
  store->recover();
  EXPECT_EQ(store->get_run(run.id).status, RunStatus::failed);
  EXPECT_EQ(store->attempts(run.id)[0].reason, "TIMEOUT");
}

TEST_F(Database, AgentRestartPreservesLiveOwnershipAndDrainStopsNewWork) {
  auto run = store->submit(spec(), random_id(), "one");
  store->register_worker("w", "old", {2000, 1024});
  auto a = store->assign("w", "old")->attempt;
  store->runtime_report("w", "old", a.id, "container", false);
  store->start(a.id, a.generation, "runner");
  store->register_worker("w", "new", {2000, 1024});
  store->drain_worker("w", true);
  store->submit(spec(), random_id(), "two");
  EXPECT_FALSE(store->assign("w", "new"));
  EXPECT_NO_THROW(store->heartbeat(a.id, a.generation, "runner"));
  EXPECT_TRUE(store->reconcile("w", "new", {a.id}).empty());
  store->cancel(run.id);
  EXPECT_EQ(store->reconcile("w", "new", {a.id, "unknown"}).size(), 2);
}

TEST_F(Database, SweepIsAtomicIdempotentAndRetainsResolvedParameters) {
  SweepSpec sweep{spec(), {{"seed", {std::int64_t{1}, std::int64_t{2}, std::int64_t{3}}}}};
  auto expanded = expand_sweep(sweep.base, sweep.grid);
  auto key = random_id();
  auto result = store->submit_sweep(sweep, expanded, key, "hash");
  ASSERT_EQ(result.run_ids.size(), 3);
  EXPECT_EQ(store->submit_sweep(sweep, expanded, key, "hash").id, result.id);
  EXPECT_THROW(store->submit_sweep(sweep, expanded, key, "different"), Error);
  for (auto &id : result.run_ids)
    EXPECT_EQ(store->get_run(id).sweep_id, result.id);
}
TEST_F(Database, RerunPreservesTheOriginalHistory) {
  auto original = store->submit(spec(), random_id(), "one");
  EXPECT_THROW(store->rerun(original.id, random_id()), Error);
  store->cancel(original.id);
  auto key = random_id();
  auto next = store->rerun(original.id, key);
  EXPECT_NE(next.id, original.id);
  EXPECT_EQ(next.parent_run_id, original.id);
  EXPECT_EQ(next.status, RunStatus::queued);
  EXPECT_EQ(store->rerun(original.id, key).id, next.id);
  EXPECT_EQ(store->get_run(original.id).status, RunStatus::cancelled);
}
