#pragma once

#include <cstddef>
#include <functional>
#include <string_view>
#include <vector>

#include "flowforge/scheduler/scheduling_policy.hpp"

namespace flowforge::scheduler {

// ---------------------------------------------------------------------------
// ReadyQueue -- ordered set of launchable tasks.
//
// Kept sorted (best-first) on insert against the active SchedulingPolicy.
// Iteration yields tasks in launch order so the scheduler can backfill: if the
// best task does not fit the resource budget, the next one is considered
// without losing the ordering. Selective removal supports cancellation.
// ---------------------------------------------------------------------------
class ReadyQueue {
public:
    explicit ReadyQueue(const SchedulingPolicy& policy) : policy_(&policy) {}

    void push(ReadyEntry entry);

    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    [[nodiscard]] const std::vector<ReadyEntry>& entries() const noexcept { return entries_; }

    /// Remove and return the best entry. Precondition: !empty().
    [[nodiscard]] ReadyEntry pop_best();

    /// Remove the entry at index @p idx (as seen in entries()).
    void erase_at(std::size_t idx);

    /// Remove the entry for @p task_id if present; returns true if removed.
    bool remove(std::string_view task_id);

    /// Drain every entry, invoking @p fn on each (used to cancel all pending).
    void drain(const std::function<void(const ReadyEntry&)>& fn);

private:
    const SchedulingPolicy* policy_;
    std::vector<ReadyEntry> entries_;  // sorted best-first
};

}  // namespace flowforge::scheduler
