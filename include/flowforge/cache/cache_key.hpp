#pragma once

#include <string>
#include <vector>

#include "flowforge/artifacts/artifact.hpp"
#include "flowforge/domain/task_definition.hpp"

namespace flowforge::cache {

inline constexpr const char* kCacheKeyPrefix = "ffcache1";

// ---------------------------------------------------------------------------
// Deterministic cache key for a task execution.
//
// Incorporated (things that change the output):
//   - key format version (kCacheKeyPrefix)
//   - task type, program, script, argument vector
//   - environment overrides (sorted by name)
//   - working directory (as declared, relative form)
//   - identity of every declared input artifact (sorted by logical name):
//     sha256 when the input opts into checksums, otherwise size + mtime
//
// Deliberately excluded (do not affect the produced files): task name,
// priority, retry policy, resource requirements, timeout.
//
// See docs/caching.md for the rationale and the invalidation rules.
// ---------------------------------------------------------------------------
[[nodiscard]] std::string compute_cache_key(const domain::TaskDefinition& task,
                                            const std::vector<artifacts::Artifact>& inputs);

}  // namespace flowforge::cache
