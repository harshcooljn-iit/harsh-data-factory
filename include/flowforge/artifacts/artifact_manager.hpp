#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "flowforge/artifacts/artifact.hpp"
#include "flowforge/domain/task_definition.hpp"

namespace flowforge::artifacts {

// ---------------------------------------------------------------------------
// ArtifactManager -- path resolution and existence checks for a task's
// declared inputs and outputs.
//
// Resolution rule (documented in docs/pipeline-format.md):
//   working dir  = task.working_directory resolved against pipeline base dir
//   artifact path = ArtifactDecl.path resolved against that working dir
// Absolute paths in either field are used as-is.
// ---------------------------------------------------------------------------
class ArtifactManager {
  public:
    explicit ArtifactManager(std::filesystem::path pipeline_base_dir);

    [[nodiscard]] std::filesystem::path resolve_working_dir(
        const domain::TaskDefinition& task) const;

    [[nodiscard]] std::filesystem::path resolve_artifact_path(
        const domain::TaskDefinition& task, const domain::ArtifactDecl& decl) const;

    [[nodiscard]] Artifact probe(const domain::TaskDefinition& task,
                                 const domain::ArtifactDecl& decl,
                                 bool force_checksum = false) const;

    /// Probe every declared input / output of @p task.
    [[nodiscard]] std::vector<Artifact> probe_inputs(const domain::TaskDefinition& task,
                                                     bool force_checksum = false) const;
    [[nodiscard]] std::vector<Artifact> probe_outputs(const domain::TaskDefinition& task,
                                                      bool force_checksum = false) const;

    /// Logical names of declared inputs that do not exist on disk.
    [[nodiscard]] std::vector<std::string> missing_inputs(
        const domain::TaskDefinition& task) const;
    /// Logical names of declared outputs that do not exist on disk.
    [[nodiscard]] std::vector<std::string> missing_outputs(
        const domain::TaskDefinition& task) const;

  private:
    std::filesystem::path base_dir_;
};

}  // namespace flowforge::artifacts
