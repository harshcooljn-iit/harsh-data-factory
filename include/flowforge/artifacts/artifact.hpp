#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace flowforge::artifacts {

// ---------------------------------------------------------------------------
// Artifact -- runtime metadata about one input/output file. FlowForge tracks
// this, not the file contents: data flows task -> file -> task and never
// through the orchestrator's memory.
// ---------------------------------------------------------------------------
struct Artifact {
    std::string logical_name;
    std::filesystem::path path;  // absolute, resolved
    bool exists = false;
    std::int64_t size_bytes = 0;
    std::int64_t modified_unix_ms = 0;
    std::optional<std::string> checksum;  // sha256 hex, only if requested

    /// Stable one-line identity used inside cache keys. Includes the checksum
    /// when present, otherwise falls back to (size, mtime).
    [[nodiscard]] std::string identity() const;
};

/// Populate an Artifact by stat()-ing @p absolute_path. @p want_checksum
/// triggers a full content hash (skipped when the file is absent).
[[nodiscard]] Artifact probe_artifact(const std::string& logical_name,
                                      const std::filesystem::path& absolute_path,
                                      bool want_checksum);

}  // namespace flowforge::artifacts
