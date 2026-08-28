#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace flowforge::process {

using EnvEntry = std::pair<std::string, std::string>;

// ---------------------------------------------------------------------------
// ProcessSpec -- an explicit, shell-free description of a child process.
//
// argv[0] is the program name as the child sees it; `program` is what is
// actually executed (they are usually equal). No string is ever handed to a
// shell: arguments are passed as a vector and reach execve() verbatim.
// ---------------------------------------------------------------------------
struct ProcessSpec {
    std::string program;
    std::vector<std::string> argv;

    /// Extra / overriding environment variables, applied after the inherited
    /// environment when `inherit_environment` is true.
    std::vector<EnvEntry> environment;
    bool inherit_environment = true;

    /// Must be an existing directory. Empty => inherit the caller's cwd.
    std::string working_directory;

    /// Wall-clock limit. nullopt => no timeout.
    std::optional<std::chrono::milliseconds> timeout;

    /// Basic well-formedness check (non-empty program, argv[0] present). Does
    /// not touch the filesystem -- that is the validator's job.
    [[nodiscard]] std::optional<std::string> validate() const;
};

/// Merge the current process environment (if requested) with `overrides` into a
/// flat "KEY=VALUE" list suitable for execve. Later entries win.
[[nodiscard]] std::vector<std::string> build_environment_block(
    const std::vector<EnvEntry>& overrides, bool inherit_current);

}  // namespace flowforge::process
