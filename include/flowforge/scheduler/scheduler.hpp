#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "flowforge/artifacts/artifact_manager.hpp"
#include "flowforge/cache/cache_store.hpp"
#include "flowforge/dag/dag.hpp"
#include "flowforge/domain/pipeline_definition.hpp"
#include "flowforge/domain/resource_requirements.hpp"
#include "flowforge/domain/run_state.hpp"
#include "flowforge/process/process_runner.hpp"
#include "flowforge/resources/resource_pool.hpp"
#include "flowforge/scheduler/ready_queue.hpp"
#include "flowforge/scheduler/scheduling_policy.hpp"

namespace flowforge::scheduler {

// ---------------------------------------------------------------------------
// SchedulerObserver -- the seam between the pure scheduling engine and its
// consumers (CLI live output, SQLite persistence). Every callback runs on the
// scheduler's own thread, in event order, so an observer may touch a
// non-thread-safe resource (like a single SQLite connection) freely.
// ---------------------------------------------------------------------------
class SchedulerObserver {
  public:
    virtual ~SchedulerObserver() = default;

    virtual void on_run_started(const domain::PipelineRun& run) { (void)run; }
    virtual void on_task_state_changed(const domain::TaskRun& task, domain::TaskState previous) {
        (void)task;
        (void)previous;
    }
    virtual void on_task_attempt_recorded(const domain::TaskRun& task,
                                          const domain::TaskAttempt& attempt) {
        (void)task;
        (void)attempt;
    }
    virtual void on_task_log(std::string_view task_id, int attempt, std::string_view stream,
                             std::string_view line) {
        (void)task_id;
        (void)attempt;
        (void)stream;
        (void)line;
    }
    virtual void on_run_finished(const domain::PipelineRun& run) { (void)run; }
};

struct SchedulerConfig {
    int max_concurrency = 4;
    domain::ResourcePool resource_capacity{};
    bool verify_outputs = true;      ///< fail a task whose declared outputs are absent
    bool force_checksums = false;    ///< checksum every artifact regardless of decl
};

struct SchedulerResult {
    domain::PipelineState state = domain::PipelineState::kCreated;
    domain::PipelineRun run;

    [[nodiscard]] bool succeeded() const noexcept {
        return state == domain::PipelineState::kSucceeded;
    }
};

// ---------------------------------------------------------------------------
// Scheduler -- dependency-aware concurrent executor for one pipeline run.
//
// Design (docs/scheduler.md):
//   * dependency counters seeded from the DAG; a task becomes READY exactly
//     when its counter hits zero. No repeated full-graph scans.
//   * one internal wait loop, driven by a condition variable that the process
//     runner's reactor thread signals on task completion / output. Never
//     busy-waits.
//   * all scheduling state is owned by run(): the only cross-thread traffic is
//     an event queue the reactor thread and cancel() push onto.
//   * one process runner thread services every child; the scheduler creates no
//     thread per task.
// ---------------------------------------------------------------------------
class Scheduler {
  public:
    Scheduler(const domain::PipelineDefinition& pipeline, const dag::Dag& graph,
              process::ProcessRunner& runner, SchedulerConfig config,
              cache::CacheStore* cache = nullptr, SchedulerObserver* observer = nullptr,
              const SchedulingPolicy* policy = nullptr);

    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    /// Execute the pipeline to a terminal state and return the outcome. May be
    /// called once per Scheduler instance.
    SchedulerResult run();

    /// Ask the running scheduler to stop: no new launches, running tasks are
    /// terminated, pending tasks become CANCELLED. Safe to call from any
    /// thread (e.g. a signal-handling thread or another controller).
    void cancel();

  private:
    struct Event {
        enum class Type { kProcessFinished, kLogLine, kCancel };
        Type type = Type::kCancel;
        std::size_t node = 0;
        process::ProcessResult result;
        std::string stream;
        std::string line;
        int attempt = 0;
    };

    enum class StartOutcome { kLaunched, kResolvedInline, kResourceBlocked };

    struct NodeState {
        domain::TaskState state = domain::TaskState::kPending;
        std::size_t remaining_deps = 0;
        int attempts_made = 0;
        process::ProcessHandle handle = process::kInvalidHandle;
        util::TimePoint retry_not_before{};
        bool waiting_for_retry = false;
        domain::ResourceRequirements reserved{};
        bool has_reservation = false;
        util::TimePoint attempt_started{};
    };

    void push_event(Event event);
    void seed_ready();
    void launch_phase();
    [[nodiscard]] StartOutcome try_start(std::size_t node);
    void start_process(std::size_t node, const domain::TaskDefinition& task);
    void handle_process_finished(const Event& event);
    void record_attempt(std::size_t node, domain::TaskState final_state,
                        const domain::TaskResult& result,
                        std::optional<std::int64_t> pid = std::nullopt);
    void on_task_succeeded(std::size_t node, bool from_cache);
    void on_task_failed(std::size_t node, const domain::TaskResult& result);
    void skip_downstream(std::size_t node);
    void mark_state(std::size_t node, domain::TaskState next);
    void release_reservation(std::size_t node);
    void begin_cancel();
    [[nodiscard]] bool all_settled() const;
    [[nodiscard]] domain::TaskResult classify(const process::ProcessResult& pr) const;
    [[nodiscard]] util::TimePoint earliest_retry_deadline() const;
    void promote_backoff_ready();
    [[nodiscard]] const domain::TaskDefinition& task_of(std::size_t node) const {
        return *task_by_node_[node];
    }
    [[nodiscard]] domain::TaskRun& run_of(std::size_t node);

    const domain::PipelineDefinition& pipeline_;
    const dag::Dag& graph_;
    process::ProcessRunner& runner_;
    SchedulerConfig config_;
    cache::CacheStore* cache_;
    SchedulerObserver* observer_;

    std::unique_ptr<SchedulingPolicy> owned_policy_;
    const SchedulingPolicy* policy_;
    ReadyQueue ready_;

    resources::ResourcePool resource_pool_;
    artifacts::ArtifactManager artifacts_;

    std::vector<NodeState> nodes_;
    std::vector<const domain::TaskDefinition*> task_by_node_;
    std::size_t running_count_ = 0;

    domain::PipelineRun run_;

    std::mutex event_mutex_;
    std::condition_variable event_cv_;
    std::deque<Event> events_;
    std::atomic<bool> cancel_requested_{false};
    bool cancelling_ = false;
    bool started_ = false;
};

}  // namespace flowforge::scheduler
