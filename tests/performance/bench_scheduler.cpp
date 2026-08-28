#include <gtest/gtest.h>

#include <string>

#include "bench.hpp"
#include "fake_process_runner.hpp"
#include "flowforge/scheduler/scheduler.hpp"
#include "pipeline_builder.hpp"

namespace {

using namespace flowforge;
using flowforge::test::bench;
using flowforge::test::FakeProcessRunner;
using flowforge::test::FakeRun;
using flowforge::test::PipelineBuilder;

FakeProcessRunner::Behavior instant() {
    return [](const process::ProcessSpec&) { return FakeRun{}; };  // exit 0, no delay
}

scheduler::SchedulerConfig cfg(int concurrency) {
    scheduler::SchedulerConfig c;
    c.max_concurrency = concurrency;
    c.resource_capacity = domain::ResourcePool{1 << 20, 1 << 20, 1 << 20};
    c.verify_outputs = false;
    return c;
}

// Deep chain: n tasks, each depends on the previous. Exercises dependency
// propagation and the wait loop, one task ready at a time.
void run_chain(int n, int concurrency) {
    PipelineBuilder b("chain");
    for (int i = 0; i < n; ++i) {
        b.task("t" + std::to_string(i));
        if (i > 0) {
            b.edge("t" + std::to_string(i - 1), "t" + std::to_string(i));
        }
    }
    auto dag = b.build_dag();
    FakeProcessRunner runner(instant());
    scheduler::Scheduler sched(b.pipeline(), dag, runner, cfg(concurrency));
    const auto r = sched.run();
    ASSERT_TRUE(r.succeeded());
}

// Wide: one root, `width` independent children, one sink. Exercises the ready
// queue and concurrency handling.
void run_wide(int width, int concurrency) {
    PipelineBuilder b("wide");
    b.task("root");
    b.task("sink");
    for (int i = 0; i < width; ++i) {
        const std::string id = "w" + std::to_string(i);
        b.task(id);
        b.edge("root", id);
        b.edge(id, "sink");
    }
    auto dag = b.build_dag();
    FakeProcessRunner runner(instant());
    scheduler::Scheduler sched(b.pipeline(), dag, runner, cfg(concurrency));
    const auto r = sched.run();
    ASSERT_TRUE(r.succeeded());
}

TEST(BenchScheduler, DeepChainThroughput) {
    std::printf("\n[scheduler: deep dependency chain, instant tasks]\n");
    for (const int n : {100, 1000, 5000}) {
        const double ms =
            bench("chain n=" + std::to_string(n) + " conc=1", 5, [&] { run_chain(n, 1); });
        EXPECT_LT(ms, 5000.0);
    }
}

TEST(BenchScheduler, WideFanoutThroughput) {
    std::printf("\n[scheduler: wide fan-out/fan-in, instant tasks]\n");
    for (const int w : {100, 1000, 3000}) {
        const double ms =
            bench("width=" + std::to_string(w) + " conc=8", 5, [&] { run_wide(w, 8); });
        EXPECT_LT(ms, 15000.0);
    }
}

}  // namespace
