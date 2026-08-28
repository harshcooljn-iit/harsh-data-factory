#pragma once

#include <cstdint>
#include <string>

namespace flowforge::domain {

// ---------------------------------------------------------------------------
// ResourceRequirements / ResourcePool
//
// A coarse accounting model. The scheduler reserves a task's requirements from
// the pool before launching it and releases them when the task reaches a
// terminal state. GPUs are accounted for as an integer count only -- no actual
// device management in 0.1.0.
// ---------------------------------------------------------------------------
struct ResourceRequirements {
    int cpu_cores = 1;
    std::int64_t memory_mb = 0;
    int gpu_count = 0;

    [[nodiscard]] static ResourceRequirements single_core() noexcept {
        return ResourceRequirements{};
    }
    [[nodiscard]] bool operator==(const ResourceRequirements&) const = default;
};

struct ResourcePool {
    int total_cpu_cores = 0;
    std::int64_t total_memory_mb = 0;
    int total_gpu_count = 0;

    [[nodiscard]] bool operator==(const ResourcePool&) const = default;
};

/// True if a single task requiring @p req could ever run on @p pool (ignoring
/// what is currently reserved). A task that can never fit is a validation
/// error, not something the scheduler should wait forever on.
[[nodiscard]] bool fits_within(const ResourceRequirements& req, const ResourcePool& pool) noexcept;

/// Human-readable summary, e.g. "cpu=2, mem=512MB, gpu=1".
[[nodiscard]] std::string describe(const ResourceRequirements& req);

}  // namespace flowforge::domain
