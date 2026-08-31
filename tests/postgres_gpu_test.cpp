#include "database_fixture.hpp"
#include "runyard/application/capacity.hpp"
#include "runyard/domain/error.hpp"
#include "runyard/serialization/json.hpp"
#include "runyard/support/crypto.hpp"
#include <future>

TEST_F(Database, GpuInventoryIsSessionFencedAndSequenceOrdered) {
  store->register_worker("gpu", "s", {4000, 4096}, {"engine", true});
  GpuSnapshot first{1, true, {{"GPU-a", "Example GPU", 24576, true, ""}}};
  store->report_gpu_inventory("gpu", "s", first);
  store->report_gpu_inventory("gpu", "s", {2, false, {}});
  store->report_gpu_inventory("gpu", "s", first);
  EXPECT_THROW(store->report_gpu_inventory("gpu", "old", {3, true, {}}), Error);
  store->register_worker("other", "s", {4000, 4096}, {"engine", true});
  EXPECT_THROW(store->report_gpu_inventory("other", "s", first), Error);
  auto c = pool->acquire();
  pqxx::read_transaction tx(c.get());
  auto workers = tx.exec("SELECT gpu_ready,gpu_sequence FROM workers WHERE id='gpu'");
  EXPECT_FALSE(workers[0][0].as<bool>());
  EXPECT_EQ(workers[0][1].as<int>(), 2);
  EXPECT_EQ(tx.exec("SELECT count(*) FROM gpu_devices WHERE present")[0][0].as<int>(), 1);
}
TEST_F(Database, GpuSchemaEnforcesExclusiveUnreleasedAllocations) {
  store->register_worker("gpu", "s", {4000, 4096}, {"engine", true});
  store->report_gpu_inventory("gpu", "s", {1, true, {{"GPU-a", "Example", 100, true, ""}}});
  auto one = store->submit(spec(), "one", "one");
  auto two = store->submit(spec(), "two", "two");
  auto c = pool->acquire();
  pqxx::work tx(c.get());
  tx.exec("INSERT INTO attempts(id,run_id,generation,worker_id,launch_deadline) "
          "VALUES('a',$1,1,'gpu',clock_timestamp()),('b',$2,1,'gpu',clock_timestamp())",
          pqxx::params{one.id, two.id});
  tx.exec("INSERT INTO attempt_gpus(attempt_id,uuid,name,memory_mib) "
          "VALUES('a','GPU-a','Example',100)");
  EXPECT_THROW(tx.exec("INSERT INTO attempt_gpus(attempt_id,uuid,name,memory_mib) "
                       "VALUES('b','GPU-a','Example',100)"),
               pqxx::unique_violation);
}
TEST_F(Database, WorkerEngineCannotChangeWithPendingCleanup) {
  store->register_worker("gpu", "s", {1000, 512}, {"engine", true});
  store->submit(spec(), "one", "one");
  auto attempt = store->assign("gpu", "s");
  ASSERT_TRUE(attempt);
  EXPECT_THROW(store->register_worker("gpu", "next", {1000, 512}, {"replacement", true}), Error);
  store->cancel(attempt->attempt.run_id);
  store->runtime_report("gpu", "s", attempt->attempt.id, "", true);
  EXPECT_NO_THROW(store->register_worker("gpu", "next", {1000, 512}, {"replacement", true}));
}
TEST_F(Database, GpuRequestsSurviveSubmissionSweepAndRerun) {
  auto gpu = spec();
  gpu.resources.gpu_count = 2;
  auto run = store->submit(gpu, "gpu", sha256(encode(gpu).dump()));
  EXPECT_EQ(store->get_run(run.id).spec.resources.gpu_count, 2);
  store->register_worker("cpu", "s", {10000, 10000});
  EXPECT_FALSE(store->assign("cpu", "s"));
  store->cancel(run.id);
  auto rerun = store->rerun(run.id, "again");
  EXPECT_EQ(rerun.spec.resources.gpu_count, 2);
  SweepSpec sweep{gpu, {{"seed", {std::int64_t{1}, std::int64_t{2}}}}};
  auto result = store->submit_sweep(sweep, expand_sweep(gpu, sweep.grid), "sweep", "hash");
  ASSERT_EQ(result.run_ids.size(), 2);
  EXPECT_EQ(store->get_run(result.run_ids[0]).spec.resources.gpu_count, 2);
  auto c = pool->acquire();
  pqxx::read_transaction tx(c.get());
  EXPECT_EQ(tx.exec("SELECT count(*) FROM runs WHERE gpu_count=2")[0][0].as<int>(), 4);
}
namespace {
void enable_gpu(PostgresStore &store, const std::string &session = "s", int count = 1) {
  store.register_worker("gpu", session, {16000, 16384}, {"engine", true});
  GpuSnapshot snapshot{1, true, {}};
  for (int i = 0; i < count; ++i)
    snapshot.devices.push_back({"GPU-" + std::to_string(i), "Device", 24576, true, ""});
  store.report_gpu_inventory("gpu", session, snapshot);
}
} // namespace
TEST_F(Database, GpuReservationReplaysAndSurvivesCancellationUntilCleanup) {
  enable_gpu(*store);
  auto s = spec();
  s.resources.gpu_count = 1;
  auto one = store->submit(s, "one", "one");
  store->submit(s, "two", "two");
  auto first = store->assign("gpu", "s");
  ASSERT_TRUE(first);
  ASSERT_EQ(first->attempt.gpu_allocations.size(), 1);
  EXPECT_EQ(first->attempt.gpu_allocations[0].device.uuid, "GPU-0");
  EXPECT_EQ(store->assign("gpu", "s")->attempt.id, first->attempt.id);
  store->runtime_report("gpu", "s", first->attempt.id, "container", false);
  store->cancel(one.id);
  EXPECT_FALSE(store->assign("gpu", "s"));
  EXPECT_EQ(store->workers()[0].reserved.gpu_count, 1);
  store->runtime_report("gpu", "s", first->attempt.id, "", true);
  EXPECT_TRUE(store->assign("gpu", "s"));
  auto historical = store->attempts(one.id)[0].gpu_allocations;
  ASSERT_EQ(historical.size(), 1);
  EXPECT_FALSE(historical[0].released_at.empty());
}
TEST_F(Database, MultiGpuClaimsAreAtomicAndSkipInfeasiblePriority) {
  enable_gpu(*store, "s", 2);
  auto too_large = spec();
  too_large.resources.gpu_count = 3;
  too_large.priority = 9;
  store->submit(too_large, "large", "large");
  auto s = spec();
  s.resources.gpu_count = 2;
  auto expected = store->submit(s, "fit", "fit");
  auto assignment = store->assign("gpu", "s");
  ASSERT_TRUE(assignment);
  EXPECT_EQ(assignment->attempt.run_id, expected.id);
  EXPECT_EQ(assignment->attempt.gpu_allocations.size(), 2);
  store->runtime_report("gpu", "s", assignment->attempt.id, "container", false);
  auto cpu = store->submit(spec(), "cpu", "cpu");
  EXPECT_EQ(store->assign("gpu", "s")->attempt.run_id, cpu.id);
}
TEST_F(Database, ConcurrentGpuPollsCannotAllocateADeviceTwice) {
  enable_gpu(*store);
  auto s = spec();
  s.resources.gpu_count = 1;
  store->submit(s, "one", "one");
  store->submit(s, "two", "two");
  auto a = std::async(std::launch::async, [&] { return store->assign("gpu", "s"); });
  auto b = std::async(std::launch::async, [&] { return store->assign("gpu", "s"); });
  auto first = a.get(), second = b.get();
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_EQ(first->attempt.id, second->attempt.id);
  EXPECT_EQ(store->workers()[0].reserved.gpu_count, 1);
}
TEST_F(Database, GpuInventoryFailureAndRestartPreserveReservations) {
  enable_gpu(*store, "s", 2);
  auto s = spec();
  s.resources.gpu_count = 1;
  store->submit(s, "one", "one");
  store->submit(s, "two", "two");
  auto first = store->assign("gpu", "s");
  ASSERT_TRUE(first);
  store->runtime_report("gpu", "s", first->attempt.id, "container", false);
  store->report_gpu_inventory("gpu", "s", {2, false, {}});
  EXPECT_FALSE(store->assign("gpu", "s"));
  EXPECT_EQ(store->workers()[0].reserved.gpu_count, 1);
  enable_gpu(*store, "new", 2);
  PostgresStore restarted(*pool);
  auto second = restarted.assign("gpu", "new");
  ASSERT_TRUE(second);
  EXPECT_NE(first->attempt.gpu_allocations[0].device.uuid,
            second->attempt.gpu_allocations[0].device.uuid);
  EXPECT_THROW(store->assign("gpu", "s"), Error);
}
TEST_F(Database, StaleGpuInventoryAndDrainBlockOnlyNewGpuClaims) {
  enable_gpu(*store);
  auto s = spec();
  s.resources.gpu_count = 1;
  store->submit(s, "gpu", "gpu");
  {
    auto c = pool->acquire();
    pqxx::work tx(c.get());
    tx.exec("UPDATE workers SET gpu_observed_at=clock_timestamp()-interval '31 seconds'");
    tx.commit();
  }
  EXPECT_FALSE(store->assign("gpu", "s"));
  auto cpu = store->submit(spec(), "cpu", "cpu");
  auto a = store->assign("gpu", "s");
  ASSERT_TRUE(a);
  EXPECT_EQ(a->attempt.run_id, cpu.id);
  store->runtime_report("gpu", "s", a->attempt.id, "container", false);
  store->report_gpu_inventory("gpu", "s", {2, true, {{"GPU-0", "Device", 100, true, ""}}});
  store->drain_worker("gpu", true);
  EXPECT_FALSE(store->assign("gpu", "s"));
}
TEST_F(Database, KubernetesGpuClaimRecordsOnlyTheWinningNode) {
  auto s = spec();
  s.resources.gpu_count = 2;
  auto run = store->submit(s, "gpu", "gpu");
  auto assignment = store->admit_kubernetes(10);
  ASSERT_TRUE(assignment);
  EXPECT_EQ(assignment->attempt.gpu_count, 2);
  const auto &a = assignment->attempt;
  store->start(a.id, a.generation, "one", "gpu-node-a");
  EXPECT_THROW(store->start(a.id, a.generation, "two", "gpu-node-b"), Error);
  EXPECT_EQ(store->attempts(run.id)[0].node_name, "gpu-node-a");
  EXPECT_TRUE(store->attempts(run.id)[0].gpu_allocations.empty());
}

TEST_F(Database, CapacityTotalsDoNotDependOnDetailPagination) {
  enable_gpu(*store, "s", 2);
  store->register_worker("cpu", "s", {1000, 512});
  CapacityService capacity(*store);
  auto page = capacity.get(1, "");
  EXPECT_EQ(page.gpu.capacity, 2);
  EXPECT_EQ(page.gpu.available, 2);
  ASSERT_EQ(page.workers.size(), 1);
  auto second = capacity.get(1, page.next_cursor);
  EXPECT_EQ(second.gpu.capacity, 2);
  ASSERT_EQ(second.workers.size(), 1);
  EXPECT_EQ(second.workers[0].capacity.gpu_count, 2);
  store->report_gpu_inventory("gpu", "s", {2, false, {}});
  EXPECT_TRUE(encode(capacity.get(1, ""))["gpu"]["available_estimate"].is_null());
}
TEST_F(Database, CapacityReportsAvailableDevicesEvenWhenAnotherDeviceDisappears) {
  enable_gpu(*store, "s", 2);
  auto s = spec();
  s.resources.gpu_count = 1;
  store->submit(s, "gpu", "gpu");
  auto a = store->assign("gpu", "s");
  ASSERT_TRUE(a);
  store->report_gpu_inventory("gpu", "s", {2, true, {{"GPU-1", "Device", 100, true, ""}}});
  auto capacity = store->gpu_capacity();
  EXPECT_EQ(capacity.capacity, 1);
  EXPECT_EQ(capacity.reserved, 1);
  EXPECT_EQ(capacity.available, 1);
  EXPECT_EQ(store->workers()[0].available_gpus, 1);
}
TEST_F(Database, ClusterCapacityServiceSummarizesAllNodesBeforePagination) {
  ClusterGpuSnapshot snapshot;
  snapshot.fresh = true;
  snapshot.nodes = {{"a", 4, 4, 1, true, true, true}, {"b", 8, 8, 2, true, false, false}};
  CapacityService capacity(*store, [&] { return snapshot; });
  auto page = capacity.get(1, "");
  EXPECT_EQ(page.backend, "kubernetes");
  EXPECT_EQ(page.gpu.capacity, 12);
  EXPECT_EQ(page.gpu.available, 3);
  ASSERT_EQ(page.nodes.size(), 1);
  EXPECT_EQ(capacity.get(1, page.next_cursor).nodes[0].name, "b");
  snapshot.fresh = false;
  EXPECT_TRUE(encode(capacity.get(1, ""))["gpu"]["available_estimate"].is_null());
}

TEST_F(Database, ExpiredGpuAttemptHoldsItsDevicesUntilCleanupBeforeRetry) {
  enable_gpu(*store);
  auto s = spec();
  s.resources.gpu_count = 1;
  auto run = store->submit(s, "gpu", "gpu");
  auto first = store->assign("gpu", "s")->attempt;
  store->runtime_report("gpu", "s", first.id, "container", false);
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
  EXPECT_EQ(store->gpu_capacity().reserved, 1);
  EXPECT_THROW(store->finish(first.id, first.generation, "old", 0, "", 0), Error);
  {
    auto c = pool->acquire();
    pqxx::work tx(c.get());
    tx.exec("UPDATE runs SET available_at=clock_timestamp() WHERE id=$1", pqxx::params{run.id});
    tx.commit();
  }
  store->recover();
  EXPECT_FALSE(store->assign("gpu", "s"));
  store->runtime_report("gpu", "s", first.id, "", true);
  auto next = store->assign("gpu", "s");
  ASSERT_TRUE(next);
  EXPECT_EQ(next->attempt.generation, 2);
  EXPECT_EQ(next->attempt.gpu_allocations[0].device.uuid, "GPU-0");
  auto history = store->attempts(run.id);
  ASSERT_EQ(history.size(), 2);
  EXPECT_FALSE(history[0].gpu_allocations[0].released_at.empty());
  EXPECT_TRUE(history[1].gpu_allocations[0].released_at.empty());
}

TEST_F(Database, InsufficientMultiGpuCapacityLeavesNoPartialAllocation) {
  enable_gpu(*store, "s", 2);
  auto s = spec();
  s.resources.gpu_count = 3;
  auto run = store->submit(s, "gpu", "gpu");
  EXPECT_FALSE(store->assign("gpu", "s"));
  EXPECT_TRUE(store->attempts(run.id).empty());
  EXPECT_EQ(store->gpu_capacity().reserved, 0);
  EXPECT_EQ(store->gpu_capacity().available, 2);
}
