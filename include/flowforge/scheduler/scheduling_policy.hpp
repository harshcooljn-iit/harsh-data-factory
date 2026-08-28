#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "flowforge/util/time_utils.hpp"

namespace flowforge::scheduler {

// ---------------------------------------------------------------------------
// One entry in the ready queue. `node` is the DAG node index (O(1) handle back
// into scheduler state); the rest is what the policy sorts on.
// ---------------------------------------------------------------------------
struct ReadyEntry {
    std::size_t node = 0;
    std::string task_id;
    int priority = 0;
    util::TimePoint ready_since{};
};

// ---------------------------------------------------------------------------
// SchedulingPolicy -- decides the order ready tasks are considered for launch.
// Replaceable; the engine installs DefaultSchedulingPolicy unless overridden.
// ---------------------------------------------------------------------------
class SchedulingPolicy {
  public:
    virtual ~SchedulingPolicy() = default;

    /// True if @p a should be launched before @p b. Must be a strict weak
    /// ordering (irreflexive, asymmetric, transitive) so the ready queue stays
    /// well-defined.
    [[nodiscard]] virtual bool prefer(const ReadyEntry& a, const ReadyEntry& b) const = 0;

    [[nodiscard]] virtual std::string name() const = 0;
};

// Default: higher priority first, then earlier readiness, then task id
// (lexicographic) as a deterministic final tie-break.
class DefaultSchedulingPolicy final : public SchedulingPolicy {
  public:
    [[nodiscard]] bool prefer(const ReadyEntry& a, const ReadyEntry& b) const override;
    [[nodiscard]] std::string name() const override { return "priority/fifo/id"; }
};

[[nodiscard]] std::unique_ptr<SchedulingPolicy> make_default_policy();

}  // namespace flowforge::scheduler
