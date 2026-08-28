#include "flowforge/scheduler/ready_queue.hpp"

#include <algorithm>

namespace flowforge::scheduler {

void ReadyQueue::push(ReadyEntry entry) {
    // Insert keeping entries_ sorted best-first: find the first element the new
    // entry is preferred over.
    const auto pos = std::find_if(
        entries_.begin(), entries_.end(),
        [&](const ReadyEntry& existing) { return policy_->prefer(entry, existing); });
    entries_.insert(pos, std::move(entry));
}

ReadyEntry ReadyQueue::pop_best() {
    ReadyEntry best = std::move(entries_.front());
    entries_.erase(entries_.begin());
    return best;
}

void ReadyQueue::erase_at(std::size_t idx) {
    if (idx < entries_.size()) {
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(idx));
    }
}

bool ReadyQueue::remove(std::string_view task_id) {
    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [&](const ReadyEntry& e) { return e.task_id == task_id; });
    if (it == entries_.end()) {
        return false;
    }
    entries_.erase(it);
    return true;
}

void ReadyQueue::drain(const std::function<void(const ReadyEntry&)>& fn) {
    for (const auto& e : entries_) {
        fn(e);
    }
    entries_.clear();
}

}  // namespace flowforge::scheduler
