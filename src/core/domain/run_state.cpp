#include "flowforge/domain/run_state.hpp"

namespace flowforge::domain {

std::string_view to_string(ResultKind kind) noexcept {
    switch (kind) {
        case ResultKind::kSucceeded:
            return "succeeded";
        case ResultKind::kFailedExit:
            return "failed_exit";
        case ResultKind::kTimeout:
            return "timeout";
        case ResultKind::kCrashed:
            return "crashed";
        case ResultKind::kStartFailure:
            return "start_failure";
        case ResultKind::kMissingOutput:
            return "missing_output";
        case ResultKind::kMissingInput:
            return "missing_input";
        case ResultKind::kCancelled:
            return "cancelled";
    }
    return "unknown";
}

bool is_retryable(ResultKind kind) noexcept {
    switch (kind) {
        case ResultKind::kFailedExit:
        case ResultKind::kTimeout:
        case ResultKind::kCrashed:
        case ResultKind::kMissingOutput:
            return true;
        // Deterministic configuration problems and cancellation are not.
        case ResultKind::kSucceeded:
        case ResultKind::kStartFailure:
        case ResultKind::kMissingInput:
        case ResultKind::kCancelled:
            return false;
    }
    return false;
}

double TaskAttempt::duration_seconds() const noexcept {
    if (!started_at || !finished_at) {
        return 0.0;
    }
    return util::seconds_between(*started_at, *finished_at);
}

double TaskRun::duration_seconds() const noexcept {
    if (!started_at || !finished_at) {
        return 0.0;
    }
    return util::seconds_between(*started_at, *finished_at);
}

void RunCounts::add(TaskState state) noexcept {
    ++total;
    switch (state) {
        case TaskState::kPending:
            ++pending;
            break;
        case TaskState::kReady:
            ++ready;
            break;
        case TaskState::kRunning:
            ++running;
            break;
        case TaskState::kSucceeded:
            ++succeeded;
            break;
        case TaskState::kFailed:
            ++failed;
            break;
        case TaskState::kSkipped:
            ++skipped;
            break;
        case TaskState::kCancelled:
            ++cancelled;
            break;
        case TaskState::kCached:
            ++cached;
            break;
    }
}

RunCounts PipelineRun::counts() const {
    RunCounts c;
    for (const auto& entry : task_runs) {
        c.add(entry.second.state);
    }
    return c;
}

double PipelineRun::duration_seconds() const noexcept {
    if (!started_at || !finished_at) {
        return 0.0;
    }
    return util::seconds_between(*started_at, *finished_at);
}

TaskRun* PipelineRun::task(const TaskId& task_id) noexcept {
    const auto it = task_runs.find(task_id);
    return it == task_runs.end() ? nullptr : &it->second;
}

const TaskRun* PipelineRun::task(const TaskId& task_id) const noexcept {
    const auto it = task_runs.find(task_id);
    return it == task_runs.end() ? nullptr : &it->second;
}

}  // namespace flowforge::domain
