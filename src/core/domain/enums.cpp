#include "flowforge/domain/enums.hpp"

#include <array>

namespace flowforge::domain {

namespace {

struct NamedTaskState {
    TaskState value;
    std::string_view name;
};

constexpr std::array kTaskStateNames{
    NamedTaskState{TaskState::kPending, "PENDING"},
    NamedTaskState{TaskState::kReady, "READY"},
    NamedTaskState{TaskState::kRunning, "RUNNING"},
    NamedTaskState{TaskState::kSucceeded, "SUCCEEDED"},
    NamedTaskState{TaskState::kFailed, "FAILED"},
    NamedTaskState{TaskState::kSkipped, "SKIPPED"},
    NamedTaskState{TaskState::kCancelled, "CANCELLED"},
    NamedTaskState{TaskState::kCached, "CACHED"},
};

struct NamedPipelineState {
    PipelineState value;
    std::string_view name;
};

constexpr std::array kPipelineStateNames{
    NamedPipelineState{PipelineState::kCreated, "CREATED"},
    NamedPipelineState{PipelineState::kRunning, "RUNNING"},
    NamedPipelineState{PipelineState::kSucceeded, "SUCCEEDED"},
    NamedPipelineState{PipelineState::kFailed, "FAILED"},
    NamedPipelineState{PipelineState::kCancelled, "CANCELLED"},
    NamedPipelineState{PipelineState::kInterrupted, "INTERRUPTED"},
};

}  // namespace

// --- TaskType --------------------------------------------------------------

std::string_view to_string(TaskType type) noexcept {
    switch (type) {
        case TaskType::kPython:
            return "python";
        case TaskType::kExecutable:
            return "executable";
    }
    return "executable";
}

std::optional<TaskType> parse_task_type(std::string_view text) noexcept {
    if (text == "python" || text == "PYTHON") {
        return TaskType::kPython;
    }
    if (text == "executable" || text == "EXECUTABLE" || text == "exec") {
        return TaskType::kExecutable;
    }
    return std::nullopt;
}

// --- TaskState -----------------------------------------------------------

std::string_view to_string(TaskState state) noexcept {
    for (const auto& entry : kTaskStateNames) {
        if (entry.value == state) {
            return entry.name;
        }
    }
    return "PENDING";
}

std::optional<TaskState> parse_task_state(std::string_view text) noexcept {
    for (const auto& entry : kTaskStateNames) {
        if (entry.name == text) {
            return entry.value;
        }
    }
    return std::nullopt;
}

bool is_terminal(TaskState state) noexcept {
    switch (state) {
        case TaskState::kSucceeded:
        case TaskState::kFailed:
        case TaskState::kSkipped:
        case TaskState::kCancelled:
        case TaskState::kCached:
            return true;
        case TaskState::kPending:
        case TaskState::kReady:
        case TaskState::kRunning:
            return false;
    }
    return false;
}

bool is_success_like(TaskState state) noexcept {
    return state == TaskState::kSucceeded || state == TaskState::kCached;
}

bool is_valid_transition(TaskState from, TaskState to) noexcept {
    if (from == to) {
        return false;
    }
    switch (from) {
        case TaskState::kPending:
            return to == TaskState::kReady || to == TaskState::kSkipped ||
                   to == TaskState::kCancelled;
        case TaskState::kReady:
            return to == TaskState::kRunning || to == TaskState::kCached ||
                   to == TaskState::kSkipped || to == TaskState::kCancelled;
        case TaskState::kRunning:
            return to == TaskState::kSucceeded || to == TaskState::kFailed ||
                   to == TaskState::kCancelled || to == TaskState::kReady;  // retry backoff
        case TaskState::kSucceeded:
        case TaskState::kFailed:
        case TaskState::kSkipped:
        case TaskState::kCancelled:
        case TaskState::kCached:
            return false;
    }
    return false;
}

// --- PipelineState -----------------------------------------------------

std::string_view to_string(PipelineState state) noexcept {
    for (const auto& entry : kPipelineStateNames) {
        if (entry.value == state) {
            return entry.name;
        }
    }
    return "CREATED";
}

std::optional<PipelineState> parse_pipeline_state(std::string_view text) noexcept {
    for (const auto& entry : kPipelineStateNames) {
        if (entry.name == text) {
            return entry.value;
        }
    }
    return std::nullopt;
}

bool is_terminal(PipelineState state) noexcept {
    switch (state) {
        case PipelineState::kSucceeded:
        case PipelineState::kFailed:
        case PipelineState::kCancelled:
        case PipelineState::kInterrupted:
            return true;
        case PipelineState::kCreated:
        case PipelineState::kRunning:
            return false;
    }
    return false;
}

}  // namespace flowforge::domain
