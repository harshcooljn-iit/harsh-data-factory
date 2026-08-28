#include "flowforge/scheduler/scheduler.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "flowforge/cache/cache_key.hpp"
#include "flowforge/domain/enums.hpp"

namespace flowforge::scheduler {

using domain::TaskState;
using util::now;

namespace {
std::int64_t ms(util::TimePoint tp) { return util::to_unix_millis(tp); }
}  // namespace

// ===========================================================================
// Construction
// ===========================================================================
Scheduler::Scheduler(const domain::PipelineDefinition& pipeline, const dag::Dag& graph,
                     process::ProcessRunner& runner, SchedulerConfig config,
                     cache::CacheStore* cache, SchedulerObserver* observer,
                     const SchedulingPolicy* policy)
    : pipeline_(pipeline),
      graph_(graph),
      runner_(runner),
      config_(config),
      cache_(cache),
      observer_(observer),
      owned_policy_(policy != nullptr ? nullptr : make_default_policy()),
      policy_(policy != nullptr ? policy : owned_policy_.get()),
      ready_(*policy_),
      resource_pool_(config.resource_capacity),
      artifacts_(pipeline.base_directory.empty() ? std::string(".") : pipeline.base_directory) {
    if (config_.max_concurrency < 1) {
        config_.max_concurrency = 1;
    }

    const std::size_t n = graph_.size();
    nodes_.resize(n);
    task_by_node_.resize(n);
    const auto& initial_counts = graph_.initial_dependency_counts();

    run_.pipeline_name = pipeline_.name;
    run_.max_concurrency = config_.max_concurrency;
    run_.created_at = now();

    for (std::size_t i = 0; i < n; ++i) {
        const std::string& id = graph_.id_at(i);
        const domain::TaskDefinition* def = pipeline_.find_task(id);
        if (def == nullptr) {
            throw std::invalid_argument("scheduler: graph node '" + id +
                                        "' has no matching task definition");
        }
        task_by_node_[i] = def;
        nodes_[i].remaining_deps = initial_counts[i];

        domain::TaskRun tr;
        tr.task_id = id;
        tr.state = TaskState::kPending;
        run_.task_runs.emplace(id, std::move(tr));
    }
}

Scheduler::~Scheduler() = default;

domain::TaskRun& Scheduler::run_of(std::size_t node) {
    return run_.task_runs.at(graph_.id_at(node));
}

// ===========================================================================
// Public API
// ===========================================================================
void Scheduler::cancel() {
    cancel_requested_.store(true, std::memory_order_release);
    push_event(Event{Event::Type::kCancel, 0, {}, {}, {}, 0});
}

void Scheduler::push_event(Event event) {
    {
        std::lock_guard<std::mutex> lock(event_mutex_);
        events_.push_back(std::move(event));
    }
    event_cv_.notify_all();
}

SchedulerResult Scheduler::run() {
    if (started_) {
        throw std::logic_error("Scheduler::run() called more than once");
    }
    started_ = true;

    run_.state = domain::PipelineState::kRunning;
    run_.started_at = now();
    if (observer_ != nullptr) {
        observer_->on_run_started(run_);
    }

    seed_ready();

    while (true) {
        if (cancel_requested_.load(std::memory_order_acquire) && !cancelling_) {
            begin_cancel();
        }

        launch_phase();

        if (all_settled()) {
            break;
        }

        std::deque<Event> batch;
        {
            std::unique_lock<std::mutex> lock(event_mutex_);
            if (events_.empty()) {
                const util::TimePoint deadline = earliest_retry_deadline();
                if (deadline != util::TimePoint::max()) {
                    event_cv_.wait_until(lock, deadline);
                } else {
                    event_cv_.wait(lock, [&] {
                        return !events_.empty() ||
                               cancel_requested_.load(std::memory_order_acquire);
                    });
                }
            }
            batch.swap(events_);
        }

        for (const auto& ev : batch) {
            switch (ev.type) {
                case Event::Type::kCancel:
                    if (!cancelling_) {
                        begin_cancel();
                    }
                    break;
                case Event::Type::kLogLine:
                    if (observer_ != nullptr) {
                        observer_->on_task_log(graph_.id_at(ev.node), ev.attempt, ev.stream,
                                               ev.line);
                    }
                    break;
                case Event::Type::kProcessFinished:
                    handle_process_finished(ev);
                    break;
            }
        }

        promote_backoff_ready();
    }

    // Drain any log lines that landed after the last wait.
    {
        std::deque<Event> tail;
        {
            std::lock_guard<std::mutex> lock(event_mutex_);
            tail.swap(events_);
        }
        for (const auto& ev : tail) {
            if (ev.type == Event::Type::kLogLine && observer_ != nullptr) {
                observer_->on_task_log(graph_.id_at(ev.node), ev.attempt, ev.stream, ev.line);
            }
        }
    }

    // ---- final pipeline state ----
    domain::RunCounts counts = run_.counts();
    if (cancelling_ || cancel_requested_.load(std::memory_order_acquire)) {
        run_.state = domain::PipelineState::kCancelled;
    } else if (counts.failed > 0) {
        run_.state = domain::PipelineState::kFailed;
    } else {
        run_.state = domain::PipelineState::kSucceeded;
    }
    run_.finished_at = now();
    if (observer_ != nullptr) {
        observer_->on_run_finished(run_);
    }
    return SchedulerResult{run_.state, run_};
}

// ===========================================================================
// Scheduling
// ===========================================================================
void Scheduler::seed_ready() {
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].remaining_deps == 0) {
            mark_state(i, TaskState::kReady);
            ready_.push(ReadyEntry{i, graph_.id_at(i), task_of(i).priority, now()});
        }
    }
}

void Scheduler::launch_phase() {
    if (cancelling_) {
        return;
    }
    bool changed = true;
    while (changed) {
        changed = false;
        if (running_count_ >= static_cast<std::size_t>(config_.max_concurrency)) {
            break;
        }
        for (std::size_t i = 0; i < ready_.entries().size(); ++i) {
            const std::size_t node = ready_.entries()[i].node;
            const std::string id = ready_.entries()[i].task_id;
            const StartOutcome outcome = try_start(node);
            if (outcome == StartOutcome::kResourceBlocked) {
                continue;
            }
            // try_start may have mutated ready_ (a cache hit can enqueue
            // downstream tasks); locate and drop this entry by id, then restart
            // the scan from a known-good state.
            ready_.remove(id);
            changed = true;
            break;
        }
    }

    // Safety net: nothing running, nothing waiting on backoff, yet tasks remain
    // ready. Only reachable if validation against the resource pool was skipped
    // and a task simply cannot be satisfied. Fail them cleanly instead of
    // spinning forever.
    if (!cancelling_ && running_count_ == 0 && !ready_.empty() &&
        earliest_retry_deadline() == util::TimePoint::max()) {
        std::vector<std::size_t> stuck;
        ready_.drain([&](const ReadyEntry& e) { stuck.push_back(e.node); });
        for (const std::size_t node : stuck) {
            domain::TaskResult r;
            r.kind = domain::ResultKind::kStartFailure;
            r.message = "task cannot be scheduled: resource requirements (" +
                        domain::describe(task_of(node).resources) +
                        ") exceed machine capacity";
            record_attempt(node, TaskState::kFailed, r);
            mark_state(node, TaskState::kFailed);
            skip_downstream(node);
        }
    }
}

Scheduler::StartOutcome Scheduler::try_start(std::size_t node) {
    const domain::TaskDefinition& task = task_of(node);

    // 1. Inputs must exist before we do anything else.
    const auto probed_inputs = artifacts_.probe_inputs(task, config_.force_checksums);
    std::vector<std::string> missing;
    for (const auto& art : probed_inputs) {
        if (!art.exists) {
            missing.push_back(art.logical_name);
        }
    }
    if (!missing.empty()) {
        domain::TaskResult r;
        r.kind = domain::ResultKind::kMissingInput;
        r.message = "required input '" + missing.front() + "' does not exist";
        record_attempt(node, TaskState::kFailed, r);
        mark_state(node, TaskState::kFailed);
        skip_downstream(node);
        return StartOutcome::kResolvedInline;
    }

    // 2. Cache.
    if (cache_ != nullptr && cache_->enabled() && task.cache_enabled) {
        const std::string key = cache::compute_cache_key(task, probed_inputs);
        if (auto hit = cache_->lookup(key, ms(now()))) {
            domain::TaskResult r = domain::TaskResult::success();
            r.exit_code = hit->exit_code;
            record_attempt(node, TaskState::kCached, r);
            mark_state(node, TaskState::kCached);
            on_task_succeeded(node, /*from_cache=*/true);
            return StartOutcome::kResolvedInline;
        }
    }

    // 3. Resources.
    if (!resource_pool_.try_reserve(task.resources)) {
        return StartOutcome::kResourceBlocked;
    }
    nodes_[node].reserved = task.resources;
    nodes_[node].has_reservation = true;

    start_process(node, task);
    return StartOutcome::kLaunched;
}

void Scheduler::start_process(std::size_t node, const domain::TaskDefinition& task) {
    NodeState& ns = nodes_[node];
    ns.attempts_made += 1;
    ns.attempt_started = now();
    const int attempt = ns.attempts_made;

    if (ns.attempts_made == 1) {
        run_of(node).started_at = ns.attempt_started;
    }
    mark_state(node, TaskState::kRunning);

    const auto cmd = task.resolve_command();
    process::ProcessSpec spec;
    spec.program = cmd.program;
    spec.argv = cmd.argv;
    spec.environment = task.environment;
    spec.inherit_environment = true;
    spec.working_directory = artifacts_.resolve_working_dir(task).string();
    spec.timeout = task.timeout;

    process::ProcessCallbacks cb;
    cb.on_stdout = [this, node, attempt](std::string_view line) {
        push_event(Event{Event::Type::kLogLine, node, {}, "stdout", std::string(line), attempt});
    };
    cb.on_stderr = [this, node, attempt](std::string_view line) {
        push_event(Event{Event::Type::kLogLine, node, {}, "stderr", std::string(line), attempt});
    };
    cb.on_exit = [this, node, attempt](const process::ProcessResult& result) {
        Event ev{Event::Type::kProcessFinished, node, result, {}, {}, attempt};
        push_event(std::move(ev));
    };

    ns.handle = runner_.launch(spec, std::move(cb));
    running_count_ += 1;
}

// ===========================================================================
// Completion handling
// ===========================================================================
domain::TaskResult Scheduler::classify(const process::ProcessResult& pr) const {
    domain::TaskResult r;
    switch (pr.outcome) {
        case process::ProcessOutcome::kExited:
            if (pr.exit_code == 0) {
                r.kind = domain::ResultKind::kSucceeded;
                r.exit_code = 0;
            } else {
                r.kind = domain::ResultKind::kFailedExit;
                r.exit_code = pr.exit_code;
                r.message = "process exited with code " + std::to_string(pr.exit_code);
            }
            break;
        case process::ProcessOutcome::kSignalled:
            r.kind = domain::ResultKind::kCrashed;
            r.term_signal = pr.term_signal;
            r.message = "process terminated by signal " + std::to_string(pr.term_signal);
            break;
        case process::ProcessOutcome::kTimedOut:
            r.kind = domain::ResultKind::kTimeout;
            r.term_signal = pr.term_signal;
            r.message = "task exceeded its timeout";
            break;
        case process::ProcessOutcome::kCancelled:
            r.kind = domain::ResultKind::kCancelled;
            r.term_signal = pr.term_signal;
            r.message = "task cancelled";
            break;
        case process::ProcessOutcome::kSpawnFailed:
            r.kind = domain::ResultKind::kStartFailure;
            r.message = pr.spawn_error.empty() ? "process failed to start" : pr.spawn_error;
            break;
    }
    return r;
}

void Scheduler::handle_process_finished(const Event& event) {
    const std::size_t node = event.node;
    const domain::TaskDefinition& task = task_of(node);

    if (running_count_ > 0) {
        running_count_ -= 1;
    }
    release_reservation(node);

    domain::TaskResult result = classify(event.result);

    if (result.ok() && config_.verify_outputs) {
        const auto missing = artifacts_.missing_outputs(task);
        if (!missing.empty()) {
            result = domain::TaskResult{};
            result.kind = domain::ResultKind::kMissingOutput;
            result.message =
                "task exited 0 but declared output '" + missing.front() + "' was not produced";
        }
    }

    const bool ok = result.ok();
    record_attempt(node, ok ? TaskState::kSucceeded : TaskState::kFailed, result,
                   event.result.pid);

    if (ok) {
        mark_state(node, TaskState::kSucceeded);
        on_task_succeeded(node, /*from_cache=*/false);
        return;
    }

    if (cancelling_ || result.kind == domain::ResultKind::kCancelled) {
        mark_state(node, TaskState::kCancelled);
        return;
    }
    on_task_failed(node, result);
}

void Scheduler::on_task_failed(std::size_t node, const domain::TaskResult& result) {
    const domain::TaskDefinition& task = task_of(node);
    NodeState& ns = nodes_[node];

    if (result.retryable() && task.retry.should_retry(ns.attempts_made)) {
        const int next_attempt = ns.attempts_made + 1;
        ns.retry_not_before = now() + task.retry.delay_before(next_attempt);
        ns.waiting_for_retry = true;
        mark_state(node, TaskState::kReady);
        promote_backoff_ready();  // fire immediately when the delay is zero
        return;
    }

    mark_state(node, TaskState::kFailed);
    skip_downstream(node);
}

void Scheduler::on_task_succeeded(std::size_t node, bool from_cache) {
    const domain::TaskDefinition& task = task_of(node);

    if (!from_cache && cache_ != nullptr && cache_->enabled() && task.cache_enabled) {
        cache::CachedOutcome outcome;
        outcome.exit_code = 0;
        for (const auto& art :
             artifacts_.probe_outputs(task, /*force_checksum=*/config_.force_checksums)) {
            cache::CachedArtifact snap;
            snap.logical_name = art.logical_name;
            snap.path = art.path.string();
            snap.size_bytes = art.size_bytes;
            snap.modified_unix_ms = art.modified_unix_ms;
            snap.checksum = art.checksum;
            outcome.outputs.push_back(std::move(snap));
        }
        const std::string key = cache::compute_cache_key(
            task, artifacts_.probe_inputs(task, config_.force_checksums));
        cache_->store(key, task.id, outcome, ms(now()));
    }

    // Dependency propagation: O(out-degree), no graph rescans.
    for (const std::string& dep_id : graph_.dependents(graph_.id_at(node))) {
        const auto didx = graph_.index_of(dep_id);
        if (!didx) {
            continue;
        }
        NodeState& dep = nodes_[*didx];
        if (dep.remaining_deps > 0) {
            dep.remaining_deps -= 1;
        }
        if (dep.remaining_deps == 0 && dep.state == TaskState::kPending && !cancelling_) {
            mark_state(*didx, TaskState::kReady);
            ready_.push(ReadyEntry{*didx, dep_id, task_of(*didx).priority, now()});
        }
    }
}

void Scheduler::skip_downstream(std::size_t node) {
    std::vector<std::size_t> stack{node};
    while (!stack.empty()) {
        const std::size_t cur = stack.back();
        stack.pop_back();
        for (const std::string& dep_id : graph_.dependents(graph_.id_at(cur))) {
            const auto didx = graph_.index_of(dep_id);
            if (!didx) {
                continue;
            }
            NodeState& dep = nodes_[*didx];
            if (domain::is_terminal(dep.state) || dep.state == TaskState::kRunning) {
                continue;
            }
            ready_.remove(dep_id);
            dep.waiting_for_retry = false;
            mark_state(*didx, TaskState::kSkipped);
            stack.push_back(*didx);
        }
    }
}

// ===========================================================================
// Cancellation
// ===========================================================================
void Scheduler::begin_cancel() {
    cancelling_ = true;

    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].state == TaskState::kRunning &&
            nodes_[i].handle != process::kInvalidHandle) {
            runner_.request_terminate(nodes_[i].handle, /*as_cancellation=*/true);
        }
    }

    ready_.drain([&](const ReadyEntry& e) { mark_state(e.node, TaskState::kCancelled); });

    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        NodeState& ns = nodes_[i];
        if (ns.waiting_for_retry) {
            ns.waiting_for_retry = false;
            mark_state(i, TaskState::kCancelled);
        } else if (ns.state == TaskState::kPending) {
            mark_state(i, TaskState::kCancelled);
        }
    }
}

// ===========================================================================
// Helpers
// ===========================================================================
void Scheduler::mark_state(std::size_t node, TaskState next) {
    NodeState& ns = nodes_[node];
    const TaskState prev = ns.state;
    if (prev == next) {
        return;
    }
    ns.state = next;

    domain::TaskRun& tr = run_of(node);
    tr.state = next;
    const util::TimePoint stamp = now();
    if (next == TaskState::kReady && !tr.ready_at) {
        tr.ready_at = stamp;
    }
    if (next == TaskState::kRunning && !tr.started_at) {
        tr.started_at = stamp;
    }
    if (domain::is_terminal(next) && !tr.finished_at) {
        tr.finished_at = stamp;
    }

    if (observer_ != nullptr) {
        observer_->on_task_state_changed(tr, prev);
    }
}

void Scheduler::record_attempt(std::size_t node, TaskState final_state,
                               const domain::TaskResult& result,
                               std::optional<std::int64_t> pid) {
    NodeState& ns = nodes_[node];
    domain::TaskAttempt att;
    att.attempt_number = std::max(1, ns.attempts_made);
    att.final_state = final_state;
    att.pid = pid;
    att.started_at = ns.attempt_started.time_since_epoch().count() != 0
                         ? std::optional<util::TimePoint>(ns.attempt_started)
                         : std::optional<util::TimePoint>(now());
    att.finished_at = now();
    att.result = result;

    domain::TaskRun& tr = run_of(node);
    tr.attempts.push_back(att);
    if (observer_ != nullptr) {
        observer_->on_task_attempt_recorded(tr, tr.attempts.back());
    }
}

void Scheduler::release_reservation(std::size_t node) {
    NodeState& ns = nodes_[node];
    if (ns.has_reservation) {
        resource_pool_.release(ns.reserved);
        ns.has_reservation = false;
    }
}

bool Scheduler::all_settled() const {
    if (running_count_ != 0 || !ready_.empty()) {
        return false;
    }
    for (const auto& ns : nodes_) {
        if (ns.waiting_for_retry) {
            return false;
        }
    }
    return true;
}

util::TimePoint Scheduler::earliest_retry_deadline() const {
    util::TimePoint earliest = util::TimePoint::max();
    for (const auto& ns : nodes_) {
        if (ns.waiting_for_retry && ns.retry_not_before < earliest) {
            earliest = ns.retry_not_before;
        }
    }
    return earliest;
}

void Scheduler::promote_backoff_ready() {
    if (cancelling_) {
        return;
    }
    const util::TimePoint stamp = now();
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        NodeState& ns = nodes_[i];
        if (ns.waiting_for_retry && ns.retry_not_before <= stamp) {
            ns.waiting_for_retry = false;
            ready_.push(ReadyEntry{i, graph_.id_at(i), task_of(i).priority, stamp});
        }
    }
}

}  // namespace flowforge::scheduler
