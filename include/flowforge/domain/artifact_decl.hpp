#pragma once

#include <string>

namespace flowforge::domain {

// ---------------------------------------------------------------------------
// ArtifactDecl -- an input or output file declared by a task in the pipeline
// definition. This is the *static* declaration; the runtime Artifact (with
// size / mtime / checksum) lives in flowforge/artifacts.
//
//   name  logical handle, unique within the task's input or output list
//   path  filesystem path, resolved relative to the task working directory
//         (which itself is resolved relative to the pipeline base directory)
// ---------------------------------------------------------------------------
struct ArtifactDecl {
    std::string name;
    std::string path;
    bool checksum = false;  ///< compute/track a content hash for this artifact

    [[nodiscard]] bool operator==(const ArtifactDecl&) const = default;
};

}  // namespace flowforge::domain
