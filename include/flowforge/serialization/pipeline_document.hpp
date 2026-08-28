#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "flowforge/domain/pipeline_definition.hpp"

namespace flowforge::serialization {

// ---------------------------------------------------------------------------
// A single problem found while reading a pipeline document. `location` is a
// human path into the document, e.g. "tasks[2].script".
// ---------------------------------------------------------------------------
struct LoadError {
    std::string location;
    std::string message;

    [[nodiscard]] std::string to_string() const;
};

struct LoadResult {
    std::optional<domain::PipelineDefinition> pipeline;
    std::vector<LoadError> errors;

    [[nodiscard]] bool ok() const noexcept { return pipeline.has_value() && errors.empty(); }
};

// ---------------------------------------------------------------------------
// Parsing is total: it never throws. Malformed JSON, wrong types and missing
// required fields all come back as LoadError entries. Structural graph
// validation (cycles, dangling edges, executable existence) is a separate
// concern handled by validation::PipelineValidator.
// ---------------------------------------------------------------------------
[[nodiscard]] LoadResult load_pipeline_from_json(std::string_view json_text,
                                                 std::string base_directory);

[[nodiscard]] LoadResult load_pipeline_from_file(const std::filesystem::path& file);

// ---------------------------------------------------------------------------
// Serialisation back to the canonical document form. The conversion layer
// keeps JSON details out of the core domain types.
// ---------------------------------------------------------------------------
[[nodiscard]] std::string dump_pipeline(const domain::PipelineDefinition& pipeline,
                                        int indent = 2);

}  // namespace flowforge::serialization
