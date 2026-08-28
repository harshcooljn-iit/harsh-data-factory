#include <gtest/gtest.h>

#include "flowforge/scheduler/ready_queue.hpp"
#include "flowforge/scheduler/scheduling_policy.hpp"

namespace {

using namespace flowforge::scheduler;
using flowforge::util::from_unix_millis;

ReadyEntry entry(std::size_t node, std::string id, int priority, std::int64_t ready_ms) {
    return ReadyEntry{node, std::move(id), priority, from_unix_millis(ready_ms)};
}

TEST(ReadyQueue, OrdersByPriorityThenReadyTimeThenId) {
    DefaultSchedulingPolicy policy;
    ReadyQueue q(policy);
    q.push(entry(0, "low_late", 0, 200));
    q.push(entry(1, "low_early", 0, 100));
    q.push(entry(2, "high", 5, 300));
    q.push(entry(3, "low_early_b", 0, 100));  // ties with low_early -> id breaks

    EXPECT_EQ(q.pop_best().task_id, "high");
    EXPECT_EQ(q.pop_best().task_id, "low_early");
    EXPECT_EQ(q.pop_best().task_id, "low_early_b");
    EXPECT_EQ(q.pop_best().task_id, "low_late");
    EXPECT_TRUE(q.empty());
}

TEST(ReadyQueue, RemoveByIdSupportsCancellation) {
    DefaultSchedulingPolicy policy;
    ReadyQueue q(policy);
    q.push(entry(0, "a", 0, 1));
    q.push(entry(1, "b", 0, 2));
    q.push(entry(2, "c", 0, 3));

    EXPECT_TRUE(q.remove("b"));
    EXPECT_FALSE(q.remove("b"));
    EXPECT_EQ(q.size(), 2u);
    EXPECT_EQ(q.pop_best().task_id, "a");
    EXPECT_EQ(q.pop_best().task_id, "c");
}

TEST(ReadyQueue, DrainVisitsEveryEntryAndEmpties) {
    DefaultSchedulingPolicy policy;
    ReadyQueue q(policy);
    q.push(entry(0, "a", 1, 1));
    q.push(entry(1, "b", 3, 1));
    q.push(entry(2, "c", 2, 1));

    std::vector<std::string> seen;
    q.drain([&](const ReadyEntry& e) { seen.push_back(e.task_id); });
    EXPECT_EQ(seen, (std::vector<std::string>{"b", "c", "a"}));  // priority order preserved
    EXPECT_TRUE(q.empty());
}

}  // namespace
