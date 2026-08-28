#include <gtest/gtest.h>

#include "flowforge/storage/database.hpp"
#include "flowforge/storage/repositories.hpp"
#include "flowforge/storage/schema.hpp"

namespace {

using namespace flowforge::storage;

struct StorageTest : ::testing::Test {
    Database db{":memory:"};
    void SetUp() override { migrate_to_latest(db); }
};

TEST_F(StorageTest, MigrationIsIdempotentAndSetsVersion) {
    EXPECT_EQ(db.user_version(), kCurrentSchemaVersion);
    migrate_to_latest(db);  // no-op second time
    EXPECT_EQ(db.user_version(), kCurrentSchemaVersion);
}

TEST_F(StorageTest, RejectsNewerSchema) {
    db.set_user_version(kCurrentSchemaVersion + 5);
    EXPECT_THROW(migrate_to_latest(db), DatabaseError);
}

TEST_F(StorageTest, PipelineUpsertIsDedupedByHash) {
    PipelineRepository repo(db);
    const auto a = repo.upsert("p", "{}", "hash-1", 1000);
    const auto b = repo.upsert("p", "{}", "hash-1", 2000);
    const auto c = repo.upsert("p", "{}", "hash-2", 3000);
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_EQ(repo.get(a)->definition_hash, "hash-1");
}

TEST_F(StorageTest, PipelineRunLifecycle) {
    PipelineRepository pipes(db);
    PipelineRunRepository runs(db);
    const auto pid = pipes.upsert("p", "{}", "h", 1);

    const auto rid = runs.create(pid, "p", 4, "CREATED", 10);
    runs.mark_started(rid, 20);
    auto rec = runs.get(rid);
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(rec->state, "RUNNING");
    EXPECT_EQ(rec->started_at.value(), 20);
    EXPECT_FALSE(rec->finished_at.has_value());

    runs.mark_finished(rid, "SUCCEEDED", 50);
    rec = runs.get(rid);
    EXPECT_EQ(rec->state, "SUCCEEDED");
    EXPECT_EQ(rec->finished_at.value(), 50);

    EXPECT_EQ(runs.list_recent(10).size(), 1u);
    EXPECT_EQ(runs.list_in_state("SUCCEEDED").size(), 1u);
    EXPECT_TRUE(runs.list_in_state("RUNNING").empty());
}

TEST_F(StorageTest, TaskRunAndAttemptsPersist) {
    PipelineRepository pipes(db);
    PipelineRunRepository runs(db);
    TaskRunRepository tasks(db);
    TaskAttemptRepository attempts(db);

    const auto pid = pipes.upsert("p", "{}", "h", 1);
    const auto rid = runs.create(pid, "p", 1, "RUNNING", 1);
    const auto trid = tasks.create(rid, "build", "PENDING");
    tasks.set_state(trid, "RUNNING");
    tasks.set_started_at(trid, 100);

    TaskAttemptRecord att;
    att.task_run_id = trid;
    att.attempt_number = 1;
    att.state = "FAILED";
    att.result_kind = "failed_exit";
    att.exit_code = 1;
    att.started_at = 100;
    att.finished_at = 110;
    att.message = "boom";
    attempts.insert(att);

    att.attempt_number = 2;
    att.state = "SUCCEEDED";
    att.result_kind = "succeeded";
    att.exit_code = 0;
    att.message.reset();
    attempts.insert(att);

    tasks.set_state(trid, "SUCCEEDED");
    tasks.set_finished_at(trid, 200);
    tasks.set_attempts(trid, 2);

    const auto list = attempts.list_for_task_run(trid);
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].attempt_number, 1);
    EXPECT_EQ(list[0].message.value(), "boom");
    EXPECT_EQ(list[1].state, "SUCCEEDED");
    EXPECT_FALSE(list[1].message.has_value());

    const auto truns = tasks.list_for_run(rid);
    ASSERT_EQ(truns.size(), 1u);
    EXPECT_EQ(truns[0].state, "SUCCEEDED");
    EXPECT_EQ(truns[0].attempts, 2);
}

TEST_F(StorageTest, LogBatchInsertAndFilter) {
    PipelineRepository pipes(db);
    PipelineRunRepository runs(db);
    const auto pid = pipes.upsert("p", "{}", "h", 1);
    const auto rid = runs.create(pid, "p", 1, "RUNNING", 1);

    LogRepository logs(db);
    logs.insert_batch({
        LogRecord{rid, std::string("a"), 1, 10, "info", "stdout", "hello from a"},
        LogRecord{rid, std::string("b"), 1, 11, "error", "stderr", "b failed"},
        LogRecord{rid, std::nullopt, std::nullopt, 12, "info", "engine", "run complete"},
    });

    EXPECT_EQ(logs.query(rid, std::nullopt, 100).size(), 3u);
    const auto only_a = logs.query(rid, std::string("a"), 100);
    ASSERT_EQ(only_a.size(), 1u);
    EXPECT_EQ(only_a[0].message, "hello from a");
}

TEST_F(StorageTest, CacheStoreLookupTouchClear) {
    CacheRepository cache(db);
    EXPECT_FALSE(cache.lookup("k1").has_value());

    cache.store("k1", "task-a", 0, R"({"outputs":[]})", 1000);
    auto rec = cache.lookup("k1");
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(rec->task_id, "task-a");
    EXPECT_EQ(rec->hit_count, 0);

    cache.touch("k1", 2000);
    rec = cache.lookup("k1");
    EXPECT_EQ(rec->hit_count, 1);
    EXPECT_EQ(rec->last_used_at, 2000);

    cache.store("k2", "task-b", 0, "{}", 1500);
    EXPECT_EQ(cache.count(), 2);
    // k1.last_used_at == 2000 (touched), k2.last_used_at == 1500.
    EXPECT_EQ(cache.prune_older_than(1600), 1);  // removes k2 only
    EXPECT_EQ(cache.count(), 1);
    EXPECT_EQ(cache.clear_all(), 1);
    EXPECT_EQ(cache.count(), 0);
}

TEST_F(StorageTest, ForeignKeyEnforcementIsOn) {
    // task_runs.run_id references a non-existent pipeline_run.
    TaskRunRepository tasks(db);
    EXPECT_THROW(tasks.create(999999, "x", "PENDING"), DatabaseError);
}

}  // namespace
