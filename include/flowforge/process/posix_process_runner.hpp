#pragma once

#include <memory>

#include "flowforge/process/process_runner.hpp"

namespace flowforge::process {

// ---------------------------------------------------------------------------
// PosixProcessRunner
//
// One background "reactor" thread services *every* child process:
//   * fork + execve to spawn (chdir + dup2 + close in the async-signal-safe
//     window; no malloc between fork and exec),
//   * a single poll(2) loop drains stdout/stderr pipes line-by-line,
//   * child exit is observed as pipe EOF and confirmed with waitpid,
//   * timeouts and cancellation escalate SIGTERM -> SIGKILL.
//
// There is NO thread per process. Adding N ready tasks adds N file
// descriptors to one poll set, not N threads. Works on macOS and Linux.
// ---------------------------------------------------------------------------
class PosixProcessRunner final : public ProcessRunner {
  public:
    struct Options {
        /// Grace period between SIGTERM and SIGKILL.
        std::chrono::milliseconds terminate_grace{std::chrono::seconds{2}};
    };

    PosixProcessRunner();
    explicit PosixProcessRunner(Options options);
    ~PosixProcessRunner() override;

    PosixProcessRunner(const PosixProcessRunner&) = delete;
    PosixProcessRunner& operator=(const PosixProcessRunner&) = delete;

    [[nodiscard]] ProcessHandle launch(const ProcessSpec& spec,
                                       ProcessCallbacks callbacks) override;
    void request_terminate(ProcessHandle handle, bool as_cancellation) override;
    [[nodiscard]] std::size_t active_count() const override;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace flowforge::process
