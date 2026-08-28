#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace flowforge::storage {
class CacheRepository;
}

namespace flowforge::cache {

// Snapshot of one output file at the time a task succeeded.
struct CachedArtifact {
    std::string logical_name;
    std::string path;  // absolute
    std::int64_t size_bytes = 0;
    std::int64_t modified_unix_ms = 0;
    std::optional<std::string> checksum;
};

struct CachedOutcome {
    int exit_code = 0;
    std::vector<CachedArtifact> outputs;
};

// ---------------------------------------------------------------------------
// CacheStore -- task-result cache on top of storage::CacheRepository.
//
// Semantics (docs/caching.md):
//   * store() is only ever called for a successful execution (exit 0).
//   * lookup() returns a hit only if EVERY recorded output still exists and
//     still matches (checksum when one was recorded, otherwise byte size).
//     A stale entry is treated as a miss and deleted, never reused.
//   * a hit bumps last_used_at / hit_count.
// ---------------------------------------------------------------------------
class CacheStore {
public:
    CacheStore(storage::CacheRepository& repo, bool enabled)
        : repo_(&repo), enabled_(enabled) {}

    [[nodiscard]] bool enabled() const noexcept { return enabled_; }

    /// Validated cache hit, or nullopt for a miss / stale entry.
    [[nodiscard]] std::optional<CachedOutcome> lookup(const std::string& cache_key,
                                                      std::int64_t now_ms);

    /// Record a successful execution. No-op if the cache is disabled.
    void store(const std::string& cache_key,
               const std::string& task_id,
               const CachedOutcome& outcome,
               std::int64_t now_ms);

private:
    storage::CacheRepository* repo_;
    bool enabled_;
};

// Exposed for testing: (de)serialise the outputs payload.
[[nodiscard]] std::string serialize_outputs(const std::vector<CachedArtifact>& outputs);
[[nodiscard]] std::vector<CachedArtifact> parse_outputs(const std::string& json_text);

}  // namespace flowforge::cache
