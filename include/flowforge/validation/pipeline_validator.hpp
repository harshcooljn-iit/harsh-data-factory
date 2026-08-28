#pragma once

#include <string>
#include <vector>

#include "flowforge/domain/pipeline_definition.hpp"

namespace flowforge::resources {
class ResourcePool;
}

namespace flowforge::validation {

enum class Severity { kError, kWarning };

// ---------------------------------------------------------------------------
// One validation finding. Errors identify the offending task and field so the
// CLI can print "Task 'train': Python interpreter not found: /bad/python3"
// instead of "Invalid pipeline."
// ---------------------------------------------------------------------------
struct ValidationIssue {
    Severity severity = Severity::kError;
    std::string task_id;  // empty => pipeline-level
    std::string field;    // empty => not field-specific
    std::string message;

    [[nodiscard]] std::string to_string() const;
};

struct ValidationReport {
    std::vector<ValidationIssue> issues;

    [[nodiscard]] bool ok() const noexcept;  // no kError issues
    [[nodiscard]] bool has_warnings() const noexcept;
    [[nodiscard]] std::vector<ValidationIssue> errors() const;
    [[nodiscard]] std::vector<ValidationIssue> warnings() const;
};

struct ValidationOptions {
    /// Resolve each task's interpreter/executable (PATH search or filesystem)
    /// and fail if it is missing or not executable.
    bool check_executables = true;

    /// Check that declared inputs of root tasks exist on disk now. Off by
    /// default: intermediate inputs are produced during the run.
    bool check_root_inputs_exist = false;

    /// If set, every task's resource requirements must fit the pool's capacity.
    const resources::ResourcePool* resource_pool = nullptr;
};

[[nodiscard]] ValidationReport validate_pipeline(const domain::PipelineDefinition& pipeline,
                                                 const ValidationOptions& options = {});

}  // namespace flowforge::validation
