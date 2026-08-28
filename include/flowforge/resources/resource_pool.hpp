#pragma once

#include <mutex>
#include <string>

#include "flowforge/domain/resource_requirements.hpp"

namespace flowforge::resources {

// ---------------------------------------------------------------------------
// ResourcePool -- the runtime resource arbiter.
//
// Constructed from a capacity description (domain::ResourcePool). The
// scheduler calls try_reserve() before launching a task and release() once it
// reaches a terminal state. Reservations never exceed capacity; a request that
// could never fit even in an empty pool is rejected by can_ever_fit() at
// validation time, not left to spin here forever.
//
// Thread-safety: every public method locks an internal mutex. Contention is
// negligible -- calls happen only at task start/finish, not in a hot loop.
// ---------------------------------------------------------------------------
class ResourcePool {
  public:
    explicit ResourcePool(domain::ResourcePool capacity);

    /// Atomically reserve @p req if it currently fits. Returns false and
    /// changes nothing otherwise.
    [[nodiscard]] bool try_reserve(const domain::ResourceRequirements& req);

    /// Return a previous reservation. Clamps at zero so a double release can
    /// never manufacture capacity.
    void release(const domain::ResourceRequirements& req);

    /// True if @p req is within total capacity (ignores current reservations).
    [[nodiscard]] bool can_ever_fit(const domain::ResourceRequirements& req) const;

    [[nodiscard]] domain::ResourcePool capacity() const;
    [[nodiscard]] domain::ResourcePool available() const;

    /// e.g. "cpu 2/8, mem 512/16384MB, gpu 0/1 in use".
    [[nodiscard]] std::string describe() const;

  private:
    mutable std::mutex mutex_;
    domain::ResourcePool capacity_;
    int used_cpu_ = 0;
    std::int64_t used_memory_mb_ = 0;
    int used_gpu_ = 0;
};

}  // namespace flowforge::resources
