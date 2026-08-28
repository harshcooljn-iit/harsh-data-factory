#include "flowforge/scheduler/scheduling_policy.hpp"

namespace flowforge::scheduler {

bool DefaultSchedulingPolicy::prefer(const ReadyEntry& a, const ReadyEntry& b) const {
    if (a.priority != b.priority) {
        return a.priority > b.priority;  // higher priority first
    }
    if (a.ready_since != b.ready_since) {
        return a.ready_since < b.ready_since;  // earlier-ready first (FIFO)
    }
    return a.task_id < b.task_id;  // deterministic final tie-break
}

std::unique_ptr<SchedulingPolicy> make_default_policy() {
    return std::make_unique<DefaultSchedulingPolicy>();
}

}  // namespace flowforge::scheduler
