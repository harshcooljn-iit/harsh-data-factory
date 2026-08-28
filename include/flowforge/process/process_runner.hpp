#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "flowforge/process/process_result.hpp"
#include "flowforge/process/process_spec.hpp"

namespace flowforge::process {

/// Opaque identifier for a launched process. 0 is never a valid handle.
using ProcessHandle = std::uint64_t;
inline constexpr ProcessHandle kInvalidHandle = 0;

// ---------------------------------------------------------------------------
// Callbacks for one launched process. on_stdout / on_stderr are invoked once
// per complete line (newline stripped) as output arrives; a final partial line
// without a trailing newline is delivered at EOF. on_exit is invoked exactly
// once, after both streams have reached EOF and the child has been reaped.
//
// All three run on the runner's internal reactor thread. They must not block
// and must not call back into the runner.
// ---------------------------------------------------------------------------
struct ProcessCallbacks {
    std::function<void(std::string_view line)> on_stdout;
    std::function<void(std::string_view line)> on_stderr;
    std::function<void(const ProcessResult& result)> on_exit;
};

// ---------------------------------------------------------------------------
// ProcessRunner -- the only abstraction the scheduler/executor uses to run
// external work. Platform process management lives entirely behind this
// interface; nothing above it includes <unistd.h>.
// ---------------------------------------------------------------------------
class ProcessRunner {
  public:
    virtual ~ProcessRunner() = default;

    /// Launch `spec`. Returns kInvalidHandle only if the request could not be
    /// queued (runner shutting down); an actual spawn failure is reported
    /// asynchronously via callbacks.on_exit with outcome kSpawnFailed.
    [[nodiscard]] virtual ProcessHandle launch(const ProcessSpec& spec,
                                               ProcessCallbacks callbacks) = 0;

    /// Ask the process to stop: SIGTERM now, SIGKILL after a short grace
    /// period. `as_cancellation` only affects how the outcome is reported
    /// (kCancelled vs kTimedOut is chosen internally for timeouts).
    virtual void request_terminate(ProcessHandle handle, bool as_cancellation) = 0;

    /// Number of processes currently tracked (launched, not yet reaped).
    [[nodiscard]] virtual std::size_t active_count() const = 0;
};

// ---------------------------------------------------------------------------
// Blocking convenience wrapper for tests and simple callers. Runs `spec` to
// completion, appending captured output to `out` / `err` when non-null.
// ---------------------------------------------------------------------------
ProcessResult run_blocking(ProcessRunner& runner, const ProcessSpec& spec,
                           std::string* out = nullptr, std::string* err = nullptr);

}  // namespace flowforge::process
