#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "flowforge/domain/enums.hpp"
#include "flowforge/domain/task_definition.hpp"
#include "flowforge/util/time_utils.hpp"

namespace flowforge::domain {

using RunId = std::int64_t;
constexpr RunId kInvalidRunId = -1;

// ---------------------------------------------------------------------------
// TaskResult -- classified outcome of ONE process attempt. The `retryable`
// flag is what the scheduler consults; it is set by the executor based on the
// kind (a config error is never retryable, a non-zero exit is).
// ---------------------------------------------------------------------------
enum class ResultKind {
    kSucceeded,
    kFailedExit,      ///< process ran and exited non-zero
    kTimeout,         ///< killed after exceeding its timeout
    kCrashed,         ///< terminated by signal
    kStartFailure,    ///< could not spawn (missing interpreter/executable, ...)
    kMissingOutput,   ///< exited zero but a declared output is absent
    kMissingInput,    ///< a declared input file was absent before launch
    kCancelled,       ///< terminated due to run cancellation
};

[[nodiscard]] std::string_view to_string(ResultKind kind) noexcept;
[[nodiscard]] bool is_retryable(ResultKind kind) noexcept;

struct TaskResult {
    ResultKind kind = ResultKind::kSucceeded;
    std::optional<int> exit_code;
    std::optional<int> term_signal;
    std::string message;      ///< concise, user-facing
    std::string detail;       ///< optional longer diagnostic

    [[nodiscard]] bool ok() const noexcept { return kind == ResultKind::kSucceeded; }
    [[nodiscard]] bool retryable() const noexcept { return is_retryable(kind); }

    static TaskResult success() { return TaskResult{}; }
};

// ---------------------------------------------------------------------------
// TaskAttempt -- one execution try of a task within a run. Every attempt is
// persisted, including failed ones.
// ---------------------------------------------------------------------------
struct TaskAttempt {
    int attempt_number = 1;              // 1-based
    TaskState final_state = TaskState::kPending;
    std::optional<std::int64_t> pid;
    std::optional<util::TimePoint> started_at;
    std::optional<util::TimePoint> finished_at;
    TaskResult result{};

    [[nodiscard]] double duration_seconds() const noexcept;
};

// ---------------------------------------------------------------------------
// TaskRun -- runtime state for a single task across all its attempts.
// ---------------------------------------------------------------------------
struct TaskRun {
    TaskId task_id;
    TaskState state = TaskState::kPending;
    std::optional<util::TimePoint> ready_at;
    std::optional<util::TimePoint> started_at;
    std::optional<util::TimePoint> finished_at;
    std::vector<TaskAttempt> attempts;

    [[nodiscard]] int attempts_made() const noexcept {
        return static_cast<int>(attempts.size());
    }
    [[nodiscard]] const TaskAttempt* last_attempt() const noexcept {
        return attempts.empty() ? nullptr : &attempts.back();
    }
    [[nodiscard]] double duration_seconds() const noexcept;
};

// ---------------------------------------------------------------------------
// RunCounts -- aggregate task tallies for observability.
// ---------------------------------------------------------------------------
struct RunCounts {
    int total = 0;
    int pending = 0;
    int ready = 0;
    int running = 0;
    int succeeded = 0;
    int failed = 0;
    int skipped = 0;
    int cancelled = 0;
    int cached = 0;

    void add(TaskState state) noexcept;
    [[nodiscard]] int finished() const noexcept {
        return succeeded + failed + skipped + cancelled + cached;
    }
};

// ---------------------------------------------------------------------------
// PipelineRun -- the whole run: one row in pipeline_runs plus its task runs.
// ---------------------------------------------------------------------------
struct PipelineRun {
    RunId id = kInvalidRunId;
    std::int64_t pipeline_id = kInvalidRunId;
    std::string pipeline_name;
    PipelineState state = PipelineState::kCreated;
    util::TimePoint created_at{};
    std::optional<util::TimePoint> started_at;
    std::optional<util::TimePoint> finished_at;
    int max_concurrency = 0;

    // Keyed by task id; ordered for deterministic reporting.
    std::map<TaskId, TaskRun> task_runs;

    [[nodiscard]] RunCounts counts() const;
    [[nodiscard]] double duration_seconds() const noexcept;
    [[nodiscard]] TaskRun* task(const TaskId& task_id) noexcept;
    [[nodiscard]] const TaskRun* task(const TaskId& task_id) const noexcept;
};

}  // namespace flowforge::domain
