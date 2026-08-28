#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "flowforge/domain/artifact_decl.hpp"
#include "flowforge/domain/enums.hpp"
#include "flowforge/domain/resource_requirements.hpp"
#include "flowforge/domain/retry_policy.hpp"

namespace flowforge::domain {

using TaskId = std::string;
using EnvVar = std::pair<std::string, std::string>;

/// Fully resolved program + argument vector for a task, ready to hand to the
/// process layer. No shell involved: argv[0] is the program.
struct ResolvedCommand {
    std::string program;
    std::vector<std::string> argv;  // includes program as argv[0]
};

// ---------------------------------------------------------------------------
// TaskDefinition -- immutable description of one unit of work.
//
// This type deliberately carries NO runtime state (no pid, no attempt counter,
// no start time, no RUNNING flag). Runtime lives in domain::TaskRun.
// ---------------------------------------------------------------------------
struct TaskDefinition {
    TaskId id;
    std::string name;
    TaskType type = TaskType::kExecutable;

    // For kExecutable: `program` is the executable. `script` is unused.
    // For kPython:     `program` is the interpreter, `script` the .py file.
    std::string program;
    std::string script;
    std::vector<std::string> arguments;

    std::vector<EnvVar> environment;   // ordered, applied on top of inherited env
    std::string working_directory;     // relative to pipeline base dir, or absolute

    std::vector<ArtifactDecl> inputs;
    std::vector<ArtifactDecl> outputs;

    RetryPolicy retry{};
    ResourceRequirements resources{};
    std::optional<std::chrono::milliseconds> timeout;
    int priority = 0;                  // higher runs first among ready tasks
    bool cache_enabled = true;

    /// Resolve program + argv. For python: {interpreter, [interpreter, script, args...]}.
    [[nodiscard]] ResolvedCommand resolve_command() const;

    [[nodiscard]] bool operator==(const TaskDefinition&) const = default;
};

}  // namespace flowforge::domain
