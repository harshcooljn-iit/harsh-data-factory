#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace flowforge::domain {

// ---------------------------------------------------------------------------
// Task type
//
// The set is intentionally small. New runtimes (shell, container, ...) are
// added here plus a branch in the serialization layer that resolves the
// concrete program/argv; the scheduler and process layer never switch on it.
// ---------------------------------------------------------------------------
enum class TaskType {
    kPython,
    kExecutable,
};

// ---------------------------------------------------------------------------
// Task lifecycle state (runtime, per pipeline run).
//
//   PENDING  -> READY | SKIPPED | CANCELLED
//   READY    -> RUNNING | SKIPPED | CANCELLED | CACHED
//   RUNNING  -> SUCCEEDED | FAILED | CANCELLED | READY(retry backoff)
//
// Terminal: SUCCEEDED, FAILED, SKIPPED, CANCELLED, CACHED.
// "Success-like" (downstream may proceed): SUCCEEDED, CACHED.
// ---------------------------------------------------------------------------
enum class TaskState {
    kPending,
    kReady,
    kRunning,
    kSucceeded,
    kFailed,
    kSkipped,
    kCancelled,
    kCached,
};

// ---------------------------------------------------------------------------
// Pipeline run state.
//
//   CREATED -> RUNNING -> SUCCEEDED | FAILED | CANCELLED
//
// INTERRUPTED is only ever assigned by crash recovery to a run that was left
// RUNNING by a previous process that is no longer alive (see docs/persistence).
// ---------------------------------------------------------------------------
enum class PipelineState {
    kCreated,
    kRunning,
    kSucceeded,
    kFailed,
    kCancelled,
    kInterrupted,
};

// --- TaskType --------------------------------------------------------------
[[nodiscard]] std::string_view to_string(TaskType type) noexcept;
[[nodiscard]] std::optional<TaskType> parse_task_type(std::string_view text) noexcept;

// --- TaskState -----------------------------------------------------------
[[nodiscard]] std::string_view to_string(TaskState state) noexcept;
[[nodiscard]] std::optional<TaskState> parse_task_state(std::string_view text) noexcept;
[[nodiscard]] bool is_terminal(TaskState state) noexcept;
[[nodiscard]] bool is_success_like(TaskState state) noexcept;

/// Whether @p from -> @p to is a legal task-state transition. Used to guard the
/// runtime state machine and unit-tested exhaustively.
[[nodiscard]] bool is_valid_transition(TaskState from, TaskState to) noexcept;

// --- PipelineState -----------------------------------------------------
[[nodiscard]] std::string_view to_string(PipelineState state) noexcept;
[[nodiscard]] std::optional<PipelineState> parse_pipeline_state(
    std::string_view text) noexcept;
[[nodiscard]] bool is_terminal(PipelineState state) noexcept;

}  // namespace flowforge::domain
