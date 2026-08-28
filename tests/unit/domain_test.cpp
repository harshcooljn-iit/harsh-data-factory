#include <gtest/gtest.h>

#include <array>

#include "flowforge/domain/enums.hpp"
#include "flowforge/domain/pipeline_definition.hpp"
#include "flowforge/domain/retry_policy.hpp"
#include "flowforge/domain/run_state.hpp"
#include "flowforge/domain/task_definition.hpp"

namespace {

using namespace flowforge::domain;

TEST(Enums, TaskStateRoundTrips) {
    constexpr std::array all{TaskState::kPending,   TaskState::kReady,  TaskState::kRunning,
                             TaskState::kSucceeded, TaskState::kFailed, TaskState::kSkipped,
                             TaskState::kCancelled, TaskState::kCached};
    for (auto s : all) {
        EXPECT_EQ(parse_task_state(to_string(s)), s);
    }
    EXPECT_FALSE(parse_task_state("NOPE").has_value());
}

TEST(Enums, TerminalAndSuccessLike) {
    EXPECT_TRUE(is_terminal(TaskState::kCached));
    EXPECT_TRUE(is_terminal(TaskState::kFailed));
    EXPECT_FALSE(is_terminal(TaskState::kRunning));
    EXPECT_TRUE(is_success_like(TaskState::kSucceeded));
    EXPECT_TRUE(is_success_like(TaskState::kCached));
    EXPECT_FALSE(is_success_like(TaskState::kFailed));
}

TEST(Enums, ValidTransitions) {
    EXPECT_TRUE(is_valid_transition(TaskState::kPending, TaskState::kReady));
    EXPECT_TRUE(is_valid_transition(TaskState::kReady, TaskState::kRunning));
    EXPECT_TRUE(is_valid_transition(TaskState::kReady, TaskState::kCached));
    EXPECT_TRUE(is_valid_transition(TaskState::kRunning, TaskState::kSucceeded));
    EXPECT_TRUE(is_valid_transition(TaskState::kRunning, TaskState::kReady));  // retry

    EXPECT_FALSE(is_valid_transition(TaskState::kPending, TaskState::kRunning));
    EXPECT_FALSE(is_valid_transition(TaskState::kSucceeded, TaskState::kRunning));
    EXPECT_FALSE(is_valid_transition(TaskState::kRunning, TaskState::kRunning));
    EXPECT_FALSE(is_valid_transition(TaskState::kCancelled, TaskState::kReady));
}

TEST(Enums, PipelineStateHelpers) {
    EXPECT_EQ(parse_pipeline_state("INTERRUPTED"), PipelineState::kInterrupted);
    EXPECT_TRUE(is_terminal(PipelineState::kSucceeded));
    EXPECT_FALSE(is_terminal(PipelineState::kRunning));
}

TEST(RetryPolicy, AttemptAccounting) {
    RetryPolicy p;
    p.max_retries = 2;
    EXPECT_EQ(p.max_attempts(), 3);
    EXPECT_TRUE(p.should_retry(1));
    EXPECT_TRUE(p.should_retry(2));
    EXPECT_FALSE(p.should_retry(3));

    RetryPolicy none = RetryPolicy::none();
    EXPECT_EQ(none.max_attempts(), 1);
    EXPECT_FALSE(none.should_retry(1));
}

TEST(RetryPolicy, ExponentialBackoffWithCap) {
    RetryPolicy p;
    p.max_retries = 5;
    p.base_delay = std::chrono::milliseconds{100};
    p.backoff_multiplier = 2.0;
    p.max_delay = std::chrono::milliseconds{500};

    EXPECT_EQ(p.delay_before(1).count(), 0);
    EXPECT_EQ(p.delay_before(2).count(), 100);
    EXPECT_EQ(p.delay_before(3).count(), 200);
    EXPECT_EQ(p.delay_before(4).count(), 400);
    EXPECT_EQ(p.delay_before(5).count(), 500);  // capped
}

TEST(TaskDefinition, ResolveCommandForPython) {
    TaskDefinition t;
    t.type = TaskType::kPython;
    t.program = "python3";
    t.script = "train.py";
    t.arguments = {"clean.csv", "model.bin"};

    const auto cmd = t.resolve_command();
    EXPECT_EQ(cmd.program, "python3");
    ASSERT_EQ(cmd.argv.size(), 4u);
    EXPECT_EQ(cmd.argv[0], "python3");
    EXPECT_EQ(cmd.argv[1], "train.py");
    EXPECT_EQ(cmd.argv[2], "clean.csv");
    EXPECT_EQ(cmd.argv[3], "model.bin");
}

TEST(TaskDefinition, ResolveCommandForExecutable) {
    TaskDefinition t;
    t.type = TaskType::kExecutable;
    t.program = "./processor";
    t.arguments = {"in.bin", "out.json"};

    const auto cmd = t.resolve_command();
    EXPECT_EQ(cmd.program, "./processor");
    ASSERT_EQ(cmd.argv.size(), 3u);
    EXPECT_EQ(cmd.argv[0], "./processor");
    EXPECT_EQ(cmd.argv[1], "in.bin");
}

TEST(PipelineDefinition, FindTask) {
    PipelineDefinition p;
    TaskDefinition a;
    a.id = "a";
    TaskDefinition b;
    b.id = "b";
    p.tasks = {a, b};
    EXPECT_TRUE(p.has_task("a"));
    EXPECT_EQ(p.find_task("b")->id, "b");
    EXPECT_EQ(p.find_task("missing"), nullptr);
}

TEST(RunState, CountsAggregate) {
    PipelineRun run;
    run.task_runs["a"].state = TaskState::kSucceeded;
    run.task_runs["b"].state = TaskState::kFailed;
    run.task_runs["c"].state = TaskState::kCached;
    run.task_runs["d"].state = TaskState::kRunning;

    const auto c = run.counts();
    EXPECT_EQ(c.total, 4);
    EXPECT_EQ(c.succeeded, 1);
    EXPECT_EQ(c.failed, 1);
    EXPECT_EQ(c.cached, 1);
    EXPECT_EQ(c.running, 1);
    EXPECT_EQ(c.finished(), 3);
}

TEST(RunState, ResultKindRetryClassification) {
    EXPECT_TRUE(is_retryable(ResultKind::kFailedExit));
    EXPECT_TRUE(is_retryable(ResultKind::kTimeout));
    EXPECT_FALSE(is_retryable(ResultKind::kStartFailure));
    EXPECT_FALSE(is_retryable(ResultKind::kMissingInput));
    EXPECT_FALSE(is_retryable(ResultKind::kCancelled));
}

}  // namespace
