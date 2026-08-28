#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace flowforge::process {

// ---------------------------------------------------------------------------
// How a child process ended.
// ---------------------------------------------------------------------------
enum class ProcessOutcome {
    kExited,       ///< ran to completion; inspect exit_code
    kSignalled,    ///< terminated by a signal FlowForge did not send
    kTimedOut,     ///< FlowForge killed it after its timeout elapsed
    kCancelled,    ///< FlowForge killed it because the run was cancelled
    kSpawnFailed,  ///< could not be started at all (see spawn_error)
};

[[nodiscard]] std::string_view to_string(ProcessOutcome outcome) noexcept;

struct ProcessResult {
    ProcessOutcome outcome = ProcessOutcome::kSpawnFailed;
    int exit_code = -1;                 ///< valid iff outcome == kExited
    int term_signal = 0;               ///< signal number for signalled/timeout/cancel
    std::optional<std::int64_t> pid;   ///< set once the child was spawned
    std::string spawn_error;           ///< human-readable, only for kSpawnFailed
    std::chrono::milliseconds duration{0};

    [[nodiscard]] bool succeeded() const noexcept {
        return outcome == ProcessOutcome::kExited && exit_code == 0;
    }
};

}  // namespace flowforge::process
