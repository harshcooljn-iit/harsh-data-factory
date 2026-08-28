#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "fake_process_runner.hpp"
#include "flowforge/scheduler/scheduler.hpp"
#include "pipeline_builder.hpp"

namespace {

using namespace flowforge;
using namespace flowforge::scheduler;
using flowforge::test::FakeProcessRunner;
using flowforge::test::FakeRun;
using flowforge::test::PipelineBuilder;

// Records the order tasks reach each terminal / running state.
struct RecordingObserver : SchedulerObserver {
    std::mutex m;
    std::vector<std::string> running_order;
    std::vector<std::pair<std::string, domain::TaskState>> transitions;
    std::vector<std::string> log_lines;

    void on_task_state_changed(const domain::TaskRun& task, domain::TaskState) override {
        std::lock_guard<std::mutex> lock(m);
        transitions.emplace_back(task.task_id, task.state);
        if (task.state == domain::TaskState::kRunning) {
            running_order.push_back(task.task_id);
        }
    }
    void on_task_log(std::string_view id, int, std::string_view stream,
                     std::string_view line) override {
        std::lock_guard<std::mutex> lock(m);
        log_lines.push_back(std::string(id) + "/" + std::string(stream) + ":" +
                            std::string(line));
    }
};

// Behavior: every program succeeds instantly unless overridden.
FakeProcessRunner::Behavior scripted(std::map<std::string, FakeRun> table,
                                     FakeRun fallback = FakeRun{}) {
    return [table = std::move(table), fallback](const process::ProcessSpec& spec) -> FakeRun {
        const std::string key = spec.argv.empty() ? spec.program : spec.argv[0];
        const auto it = table.find(key);
        return it == table.end() ? fallback : it->second;
    };
}

SchedulerConfig config(int concurrency, int cpu_capacity = 64) {
    SchedulerConfig c;
    c.max_concurrency = concurrency;
    c.resource_capacity = domain::ResourcePool{cpu_capacity, 0, 0};
    c.verify_outputs = false;
    return c;
}

TEST(Scheduler, RunsLinearChainInOrder) {
    PipelineBuilder b;
    b.task("a", "a").task("b", "b").task("c", "c").edge("a", "b").edge("b", "c");
    auto dag = b.build_dag();
    FakeProcessRunner runner(scripted({}));
    RecordingObserver obs;

    Scheduler sched(b.pipeline(), dag, runner, config(4), nullptr, &obs);
    const auto result = sched.run();

    EXPECT_TRUE(result.succeeded());
    EXPECT_EQ(obs.running_order, (std::vector<std::string>{"a", "b", "c"}));
    for (const auto& id : {"a", "b", "c"}) {
        EXPECT_EQ(result.run.task(id)->state, domain::TaskState::kSucceeded);
    }
}

TEST(Scheduler, BranchAndJoinDiamond) {
    PipelineBuilder b;
    b.task("root", "root")
        .task("left", "left")
        .task("right", "right")
        .task("join", "join")
        .edge("root", "left")
        .edge("root", "right")
        .edge("left", "join")
        .edge("right", "join");
    auto dag = b.build_dag();
    FakeProcessRunner runner(scripted({}));
    RecordingObserver obs;
    Scheduler sched(b.pipeline(), dag, runner, config(4), nullptr, &obs);

    const auto result = sched.run();
    EXPECT_TRUE(result.succeeded());
    // root before the branches, join last.
    EXPECT_EQ(obs.running_order.front(), "root");
    EXPECT_EQ(obs.running_order.back(), "join");
}

TEST(Scheduler, MultipleRootsAndLeaves) {
    PipelineBuilder b;
    b.task("r1", "r1").task("r2", "r2").task("mid", "mid").task("l1", "l1").task("l2", "l2");
    b.edge("r1", "mid").edge("r2", "mid").edge("mid", "l1").edge("mid", "l2");
    auto dag = b.build_dag();
    FakeProcessRunner runner(scripted({}));
    Scheduler sched(b.pipeline(), dag, runner, config(4));
    const auto result = sched.run();
    EXPECT_TRUE(result.succeeded());
    EXPECT_EQ(result.run.counts().succeeded, 5);
}

TEST(Scheduler, RespectsConcurrencyLimit) {
    PipelineBuilder b;
    for (int i = 0; i < 6; ++i) {
        b.task("t" + std::to_string(i), "t" + std::to_string(i));
    }
    auto dag = b.build_dag();
    FakeRun slow;
    slow.duration = std::chrono::milliseconds(60);
    FakeProcessRunner runner(scripted({}, slow));
    Scheduler sched(b.pipeline(), dag, runner, config(2));

    const auto result = sched.run();
    EXPECT_TRUE(result.succeeded());
    EXPECT_LE(runner.max_concurrent(), 2u);
}

TEST(Scheduler, HigherPriorityRunsFirstWhenSlotsAreScarce) {
    PipelineBuilder b;
    b.task("low1", "low1", /*priority=*/0)
        .task("low2", "low2", 0)
        .task("high", "high", /*priority=*/10);
    auto dag = b.build_dag();
    FakeRun slow;
    slow.duration = std::chrono::milliseconds(40);
    FakeProcessRunner runner(scripted({}, slow));
    Scheduler sched(b.pipeline(), dag, runner, config(1));  // one slot -> order matters

    const auto result = sched.run();
    EXPECT_TRUE(result.succeeded());
    EXPECT_EQ(runner.launched_programs().front(), "high");
}

TEST(Scheduler, ResourceAccountingSerialisesTasksThatDoNotFit) {
    // 8 cores; A and B need 4 each (fit together), C needs 4 (must wait).
    PipelineBuilder b;
    b.task("A", "A").task("B", "B").task("C", "C");
    b.cpu("A", 4).cpu("B", 4).cpu("C", 4);
    auto dag = b.build_dag();

    FakeRun slow;
    slow.duration = std::chrono::milliseconds(50);
    FakeProcessRunner runner(scripted({}, slow));
    Scheduler sched(b.pipeline(), dag, runner, config(8, /*cpu_capacity=*/8));

    const auto result = sched.run();
    EXPECT_TRUE(result.succeeded());
    // C is launched only after one of A/B finished.
    const auto order = runner.launched_programs();
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[2], "C");
    EXPECT_LE(runner.max_concurrent(), 2u);
}

TEST(Scheduler, RetriesUntilSuccess) {
    PipelineBuilder b;
    b.task("flaky", "flaky").retries("flaky", 3);
    auto dag = b.build_dag();

    // Fail twice, then succeed: use a stateful behavior.
    std::atomic<int> attempts{0};
    FakeProcessRunner runner([&](const process::ProcessSpec&) {
        FakeRun r;
        r.exit_code = (++attempts >= 3) ? 0 : 1;
        return r;
    });
    RecordingObserver obs;
    Scheduler sched(b.pipeline(), dag, runner, config(1), nullptr, &obs);
    const auto result = sched.run();

    EXPECT_TRUE(result.succeeded());
    const auto* tr = result.run.task("flaky");
    ASSERT_NE(tr, nullptr);
    EXPECT_EQ(tr->attempts.size(), 3u);
    EXPECT_EQ(tr->attempts[0].final_state, domain::TaskState::kFailed);
    EXPECT_EQ(tr->attempts[2].final_state, domain::TaskState::kSucceeded);
    EXPECT_EQ(tr->state, domain::TaskState::kSucceeded);
}

TEST(Scheduler, RetriesExhaustedMarksFailed) {
    PipelineBuilder b;
    b.task("bad", "bad").retries("bad", 2);
    auto dag = b.build_dag();
    FakeRun fail;
    fail.exit_code = 1;
    FakeProcessRunner runner(scripted({}, fail));
    Scheduler sched(b.pipeline(), dag, runner, config(1));
    const auto result = sched.run();

    EXPECT_EQ(result.state, domain::PipelineState::kFailed);
    const auto* tr = result.run.task("bad");
    EXPECT_EQ(tr->attempts.size(), 3u);  // 1 + 2 retries
    EXPECT_EQ(tr->state, domain::TaskState::kFailed);
}

TEST(Scheduler, DownstreamSkippedButIndependentBranchContinues) {
    //     a
    //    / \
    //   b   c
    //        \
    //         d
    // b fails -> b FAILED, nothing downstream of b. c and d still run.
    PipelineBuilder b;
    b.task("a", "a").task("b", "b").task("c", "c").task("d", "d");
    b.edge("a", "b").edge("a", "c").edge("c", "d");
    auto dag = b.build_dag();

    FakeRun fail;
    fail.exit_code = 2;
    FakeProcessRunner runner(scripted({{"b", fail}}));
    Scheduler sched(b.pipeline(), dag, runner, config(4));
    const auto result = sched.run();

    EXPECT_EQ(result.state, domain::PipelineState::kFailed);
    EXPECT_EQ(result.run.task("a")->state, domain::TaskState::kSucceeded);
    EXPECT_EQ(result.run.task("b")->state, domain::TaskState::kFailed);
    EXPECT_EQ(result.run.task("c")->state, domain::TaskState::kSucceeded);
    EXPECT_EQ(result.run.task("d")->state, domain::TaskState::kSucceeded);
}

TEST(Scheduler, FailurePropagatesSkipTransitively) {
    //   a -> b -> c -> d ,  plus independent e
    PipelineBuilder b;
    b.task("a", "a").task("b", "b").task("c", "c").task("d", "d").task("e", "e");
    b.edge("a", "b").edge("b", "c").edge("c", "d");
    auto dag = b.build_dag();
    FakeRun fail;
    fail.exit_code = 1;
    FakeProcessRunner runner(scripted({{"b", fail}}));
    Scheduler sched(b.pipeline(), dag, runner, config(4));
    const auto result = sched.run();

    EXPECT_EQ(result.run.task("b")->state, domain::TaskState::kFailed);
    EXPECT_EQ(result.run.task("c")->state, domain::TaskState::kSkipped);
    EXPECT_EQ(result.run.task("d")->state, domain::TaskState::kSkipped);
    EXPECT_EQ(result.run.task("e")->state, domain::TaskState::kSucceeded);
}

TEST(Scheduler, SpawnFailureIsNotRetriedAndSkipsDownstream) {
    PipelineBuilder b;
    b.task("start", "start").task("next", "next").retries("start", 5).edge("start", "next");
    auto dag = b.build_dag();
    FakeRun bad;
    bad.spawn_failure = true;
    bad.spawn_error = "no such file";
    FakeProcessRunner runner(scripted({{"start", bad}}));
    Scheduler sched(b.pipeline(), dag, runner, config(2));
    const auto result = sched.run();

    EXPECT_EQ(result.state, domain::PipelineState::kFailed);
    EXPECT_EQ(result.run.task("start")->attempts.size(), 1u);  // start failure not retryable
    EXPECT_EQ(result.run.task("next")->state, domain::TaskState::kSkipped);
}

TEST(Scheduler, CancellationStopsPendingAndRunningTasks) {
    PipelineBuilder b;
    b.task("a", "a").task("b", "b").task("c", "c").edge("a", "b").edge("b", "c");
    auto dag = b.build_dag();

    FakeRun slow;
    slow.duration = std::chrono::milliseconds(400);
    FakeProcessRunner runner(scripted({}, slow));
    Scheduler sched(b.pipeline(), dag, runner, config(4));

    std::thread canceller([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        sched.cancel();
    });
    const auto result = sched.run();
    canceller.join();

    EXPECT_EQ(result.state, domain::PipelineState::kCancelled);
    EXPECT_EQ(result.run.task("a")->state, domain::TaskState::kCancelled);
    EXPECT_NE(result.run.task("c")->state, domain::TaskState::kSucceeded);
}

TEST(Scheduler, StreamsLogLinesToObserver) {
    PipelineBuilder b;
    b.task("noisy", "noisy");
    auto dag = b.build_dag();
    FakeRun r;
    r.stdout_lines = {"hello", "world"};
    r.stderr_lines = {"warning"};
    FakeProcessRunner runner(scripted({{"noisy", r}}));
    RecordingObserver obs;
    Scheduler sched(b.pipeline(), dag, runner, config(1), nullptr, &obs);
    sched.run();

    std::lock_guard<std::mutex> lock(obs.m);
    ASSERT_EQ(obs.log_lines.size(), 3u);
    EXPECT_EQ(obs.log_lines[0], "noisy/stdout:hello");
    EXPECT_EQ(obs.log_lines[2], "noisy/stderr:warning");
}

TEST(Scheduler, RunCannotBeInvokedTwice) {
    PipelineBuilder b;
    b.task("a", "a");
    auto dag = b.build_dag();
    FakeProcessRunner runner(scripted({}));
    Scheduler sched(b.pipeline(), dag, runner, config(1));
    sched.run();
    EXPECT_THROW(sched.run(), std::logic_error);
}

}  // namespace
