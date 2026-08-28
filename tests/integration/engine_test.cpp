#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "flowforge/engine/engine.hpp"
#include "flowforge/storage/database.hpp"
#include "flowforge/storage/repositories.hpp"
#include "flowforge/storage/schema.hpp"
#include "flowforge_test/helper_paths.hpp"

namespace {

namespace fs = std::filesystem;
using namespace flowforge;
using domain::TaskDefinition;
using domain::TaskType;
using engine::Engine;

struct EngineTest : ::testing::Test {
    fs::path root;
    engine::Config cfg;

    void SetUp() override {
        root = fs::temp_directory_path() /
               ("ff_engine_" + std::to_string(::testing::UnitTest::GetInstance()
                                                  ->current_test_info()
                                                  ->line()) +
                "_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::create_directories(root);
        cfg = engine::Config::defaults();
        cfg.state_dir = (root / ".flowforge").string();
        cfg.resource_capacity = domain::ResourcePool{8, 1 << 20, 0};
        cfg.verify_outputs = true;
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    TaskDefinition exec_task(const std::string& id, std::string program,
                             std::vector<std::string> args) {
        TaskDefinition t;
        t.id = id;
        t.name = id;
        t.type = TaskType::kExecutable;
        t.program = std::move(program);
        t.arguments = std::move(args);
        t.cache_enabled = false;
        return t;
    }

    domain::PipelineDefinition pipeline(std::string name) {
        domain::PipelineDefinition p;
        p.name = std::move(name);
        p.base_directory = root.string();
        return p;
    }
};

TEST_F(EngineTest, RunsPipelineAndPersistsRun) {
    auto p = pipeline("basic");
    p.tasks = {exec_task("a", test::kHelperEmit, {"--stdout-lines", "2"}),
               exec_task("b", test::kHelperEmit, {"--stdout-lines", "1"})};
    p.edges = {{"a", "b"}};

    Engine eng(cfg);
    const auto report = eng.run_pipeline(p, {});

    ASSERT_FALSE(report.validation_failed) << report.validation.issues.empty();
    EXPECT_EQ(report.state, domain::PipelineState::kSucceeded);
    ASSERT_NE(report.run_id, domain::kInvalidRunId);

    const auto view = eng.get_run(report.run_id);
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->state, domain::PipelineState::kSucceeded);
    EXPECT_EQ(view->counts.succeeded, 2);
    EXPECT_EQ(view->tasks.size(), 2u);

    const auto logs = eng.get_logs(report.run_id, std::nullopt, 100);
    EXPECT_FALSE(logs.empty());
    EXPECT_EQ(eng.list_runs(10).size(), 1u);
}

TEST_F(EngineTest, ValidationFailureCreatesNoRun) {
    auto p = pipeline("bad");
    p.tasks = {exec_task("x", "/no/such/executable/here", {})};

    Engine eng(cfg);
    const auto report = eng.run_pipeline(p, {});
    EXPECT_TRUE(report.validation_failed);
    EXPECT_EQ(report.run_id, domain::kInvalidRunId);
    EXPECT_TRUE(eng.list_runs(10).empty());
}

TEST_F(EngineTest, PersistsEveryRetryAttempt) {
    const auto counter = (root / "counter.txt").string();
    auto p = pipeline("retry");
    auto flaky = exec_task("flaky", test::kHelperFlaky, {counter, "3"});
    flaky.retry.max_retries = 3;
    flaky.retry.base_delay = std::chrono::milliseconds(5);
    p.tasks = {flaky};

    Engine eng(cfg);
    const auto report = eng.run_pipeline(p, {});
    EXPECT_EQ(report.state, domain::PipelineState::kSucceeded);

    const auto view = eng.get_run(report.run_id);
    ASSERT_TRUE(view.has_value());
    ASSERT_EQ(view->tasks.size(), 1u);
    EXPECT_EQ(view->tasks[0].state, domain::TaskState::kSucceeded);
    EXPECT_EQ(view->tasks[0].attempts, 3);
}

TEST_F(EngineTest, FailurePropagationSkipsDownstreamKeepsIndependentBranch) {
    auto p = pipeline("failprop");
    p.tasks = {exec_task("a", test::kHelperEmit, {"--stdout-lines", "1"}),
               exec_task("b", test::kHelperEmit, {"--exit", "3"}),
               exec_task("c", test::kHelperEmit, {"--stdout-lines", "1"}),
               exec_task("d", test::kHelperEmit, {"--stdout-lines", "1"})};
    p.edges = {{"a", "b"}, {"b", "c"}};  // d independent

    Engine eng(cfg);
    const auto report = eng.run_pipeline(p, {});
    EXPECT_EQ(report.state, domain::PipelineState::kFailed);

    const auto view = eng.get_run(report.run_id);
    ASSERT_TRUE(view.has_value());
    auto state_of = [&](const std::string& id) {
        for (const auto& t : view->tasks) {
            if (t.task_id == id) {
                return t.state;
            }
        }
        return domain::TaskState::kPending;
    };
    EXPECT_EQ(state_of("a"), domain::TaskState::kSucceeded);
    EXPECT_EQ(state_of("b"), domain::TaskState::kFailed);
    EXPECT_EQ(state_of("c"), domain::TaskState::kSkipped);
    EXPECT_EQ(state_of("d"), domain::TaskState::kSucceeded);
}

TEST_F(EngineTest, CacheHitOnSecondRun) {
    auto p = pipeline("cache");
    auto t = exec_task("make", test::kHelperEmit, {"--write", "out.txt", "--stdout-lines", "1"});
    t.cache_enabled = true;
    t.outputs = {{"out", "out.txt", false}};
    p.tasks = {t};

    Engine eng(cfg);
    const auto first = eng.run_pipeline(p, {});
    EXPECT_EQ(first.state, domain::PipelineState::kSucceeded);
    EXPECT_EQ(first.run.task("make")->state, domain::TaskState::kSucceeded);

    const auto second = eng.run_pipeline(p, {});
    EXPECT_EQ(second.state, domain::PipelineState::kSucceeded);
    EXPECT_EQ(second.run.task("make")->state, domain::TaskState::kCached);

    // Changing the arguments must bust the cache.
    p.tasks[0].arguments = {"--write", "out.txt", "--stdout-lines", "2"};
    const auto third = eng.run_pipeline(p, {});
    EXPECT_EQ(third.run.task("make")->state, domain::TaskState::kSucceeded);
}

TEST_F(EngineTest, MissingDeclaredOutputFailsTask) {
    auto p = pipeline("noout");
    auto t = exec_task("gen", test::kHelperEmit, {"--stdout-lines", "1"});  // writes nothing
    t.outputs = {{"result", "result.bin", false}};
    p.tasks = {t};

    Engine eng(cfg);
    const auto report = eng.run_pipeline(p, {});
    EXPECT_EQ(report.state, domain::PipelineState::kFailed);
    EXPECT_EQ(report.run.task("gen")->state, domain::TaskState::kFailed);
}

TEST_F(EngineTest, ExternalCancelFlagStopsRun) {
    auto p = pipeline("cancel");
    p.tasks = {exec_task("slow", test::kHelperSleeper, {"5000"})};

    Engine eng(cfg);
    std::atomic<bool> cancel{false};
    Engine::RunRequest req;
    req.external_cancel = &cancel;

    std::thread trigger([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        cancel.store(true);
    });
    const auto report = eng.run_pipeline(p, req);
    trigger.join();

    EXPECT_EQ(report.state, domain::PipelineState::kCancelled);
}

TEST_F(EngineTest, RecoverOrphanedRunsMarksThemInterrupted) {
    // Simulate a previous process that died mid-run.
    {
        fs::create_directories(fs::path(cfg.resolved_database_path()).parent_path());
        storage::Database db(cfg.resolved_database_path());
        storage::migrate_to_latest(db);
        storage::PipelineRepository pr(db);
        storage::PipelineRunRepository rr(db);
        storage::TaskRunRepository tr(db);
        const auto pid = pr.upsert("ghost", "{}", "ghost-hash", 1);
        const auto rid = rr.create(pid, "ghost", 1, "RUNNING", 1);
        tr.create(rid, "t1", "RUNNING");
    }

    Engine eng(cfg);
    EXPECT_EQ(eng.recover_orphaned_runs(), 1);

    const auto runs = eng.list_runs(10);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0].state, domain::PipelineState::kInterrupted);
}

}  // namespace
