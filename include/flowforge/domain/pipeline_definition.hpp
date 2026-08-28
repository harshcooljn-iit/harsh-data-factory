#pragma once

#include <optional>
#include <string>
#include <vector>

#include "flowforge/domain/task_definition.hpp"

namespace flowforge::domain {

// ---------------------------------------------------------------------------
// Edge -- a dependency: `to` runs only after `from` completes success-like.
// ---------------------------------------------------------------------------
struct Edge {
    TaskId from;
    TaskId to;

    [[nodiscard]] bool operator==(const Edge&) const = default;
};

// ---------------------------------------------------------------------------
// PipelineDefinition -- the immutable, validated (or about-to-be-validated)
// description of a whole pipeline. `base_directory` is the directory the
// pipeline file was loaded from; every relative task path resolves against it.
// ---------------------------------------------------------------------------
struct PipelineDefinition {
    int schema_version = 1;
    std::string name;
    std::string description;
    std::string base_directory;  // absolute; set by the loader

    std::vector<TaskDefinition> tasks;
    std::vector<Edge> edges;

    /// Pipeline-level default concurrency hint (0 = use engine config default).
    int max_concurrency = 0;

    [[nodiscard]] const TaskDefinition* find_task(const TaskId& id) const noexcept;
    [[nodiscard]] bool has_task(const TaskId& id) const noexcept;

    [[nodiscard]] bool operator==(const PipelineDefinition&) const = default;
};

}  // namespace flowforge::domain
