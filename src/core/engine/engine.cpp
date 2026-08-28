#include "flowforge/engine/engine.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unordered_map>
#include <utility>

#include "flowforge/cache/cache_store.hpp"
#include "flowforge/dag/dag.hpp"
#include "flowforge/process/posix_process_runner.hpp"
#include "flowforge/serialization/pipeline_document.hpp"
#include "flowforge/storage/database.hpp"
#include "flowforge/storage/schema.hpp"
#include "flowforge/util/sha256.hpp"
#include "flowforge/util/time_utils.hpp"

namespace fs = std::filesystem;

namespace flowforge::engine {

using domain::PipelineState;
using domain::TaskState;

namespace {

std::int64_t now_ms() {
    return util::to_unix_millis(util::now());
}

std::optional<std::int64_t> tp_ms(const std::optional<util::TimePoint>& tp) {
    if (!tp) {
        return std::nullopt;
    }
    return util::to_unix_millis(*tp);
}

// -------------------------------------------------------------------------
// PersistenceObserver -- mirrors scheduler events into SQLite. All callbacks
// run on the scheduler thread (== the engine thread), so the single DB
// connection is used without extra locking.
// -------------------------------------------------------------------------
class PersistenceObserver final : public scheduler::SchedulerObserver {
public:
    PersistenceObserver(storage::Database& db,
                        domain::RunId run_id,
                        std::unordered_map<std::string, std::int64_t> task_run_ids)
        : run_id_(run_id),
          task_runs_(db),
          attempts_(db),
          logs_(db),
          task_run_ids_(std::move(task_run_ids)) {}

    void on_task_state_changed(const domain::TaskRun& task, TaskState) override {
        const auto it = task_run_ids_.find(task.task_id);
        if (it == task_run_ids_.end()) {
            return;
        }
        const std::int64_t id = it->second;
        task_runs_.set_state(id, std::string(domain::to_string(task.state)));
        if (task.ready_at) {
            task_runs_.set_ready_at(id, util::to_unix_millis(*task.ready_at));
        }
        if (task.started_at) {
            task_runs_.set_started_at(id, util::to_unix_millis(*task.started_at));
        }
        if (task.finished_at) {
            task_runs_.set_finished_at(id, util::to_unix_millis(*task.finished_at));
        }
        task_runs_.set_attempts(id, task.attempts_made());
    }

    void on_task_attempt_recorded(const domain::TaskRun& task,
                                  const domain::TaskAttempt& att) override {
        const auto it = task_run_ids_.find(task.task_id);
        if (it == task_run_ids_.end()) {
            return;
        }
        storage::TaskAttemptRecord rec;
        rec.task_run_id = it->second;
        rec.attempt_number = att.attempt_number;
        rec.state = std::string(domain::to_string(att.final_state));
        rec.result_kind = std::string(domain::to_string(att.result.kind));
        if (att.result.exit_code) {
            rec.exit_code = *att.result.exit_code;
        }
        if (att.result.term_signal) {
            rec.term_signal = *att.result.term_signal;
        }
        rec.pid = att.pid;
        rec.started_at = tp_ms(att.started_at);
        rec.finished_at = tp_ms(att.finished_at);
        if (!att.result.message.empty()) {
            rec.message = att.result.message;
        }
        attempts_.insert(rec);
    }

    void on_task_log(std::string_view task_id,
                     int attempt,
                     std::string_view stream,
                     std::string_view line) override {
        storage::LogRecord rec;
        rec.run_id = run_id_;
        rec.task_id = std::string(task_id);
        rec.attempt_number = attempt;
        rec.ts = now_ms();
        rec.severity = stream == "stderr" ? "warn" : "info";
        rec.stream = std::string(stream);
        rec.message = std::string(line);
        buffer_.push_back(std::move(rec));
        if (buffer_.size() >= 128) {
            flush();
        }
    }

    void on_run_finished(const domain::PipelineRun& run) override {
        storage::LogRecord rec;
        rec.run_id = run_id_;
        rec.ts = now_ms();
        rec.severity = run.state == PipelineState::kSucceeded ? "info" : "error";
        rec.stream = "engine";
        rec.message = "pipeline " + std::string(domain::to_string(run.state));
        buffer_.push_back(std::move(rec));
        flush();
    }

    void flush() {
        if (!buffer_.empty()) {
            logs_.insert_batch(buffer_);
            buffer_.clear();
        }
    }

private:
    domain::RunId run_id_;
    storage::TaskRunRepository task_runs_;
    storage::TaskAttemptRepository attempts_;
    storage::LogRepository logs_;
    std::unordered_map<std::string, std::int64_t> task_run_ids_;
    std::vector<storage::LogRecord> buffer_;
};

// Fan a single scheduler event stream out to several observers.
class FanoutObserver final : public scheduler::SchedulerObserver {
public:
    void add(scheduler::SchedulerObserver* obs) {
        if (obs != nullptr) {
            sinks_.push_back(obs);
        }
    }
    void on_run_started(const domain::PipelineRun& r) override {
        for (auto* s : sinks_)
            s->on_run_started(r);
    }
    void on_task_state_changed(const domain::TaskRun& t, TaskState p) override {
        for (auto* s : sinks_)
            s->on_task_state_changed(t, p);
    }
    void on_task_attempt_recorded(const domain::TaskRun& t,
                                  const domain::TaskAttempt& a) override {
        for (auto* s : sinks_)
            s->on_task_attempt_recorded(t, a);
    }
    void on_task_log(std::string_view id,
                     int attempt,
                     std::string_view stream,
                     std::string_view line) override {
        for (auto* s : sinks_)
            s->on_task_log(id, attempt, stream, line);
    }
    void on_run_finished(const domain::PipelineRun& r) override {
        for (auto* s : sinks_)
            s->on_run_finished(r);
    }

private:
    std::vector<scheduler::SchedulerObserver*> sinks_;
};

}  // namespace

// ===========================================================================
// Engine::Impl
// ===========================================================================
class Engine::Impl {
public:
    explicit Impl(const Config& config) : db_(open_db(config)) {
        storage::migrate_to_latest(db_);
    }

    static std::string open_db(const Config& config) {
        const std::string path = config.resolved_database_path();
        const fs::path parent = fs::path(path).parent_path();
        if (!parent.empty()) {
            std::error_code ec;
            fs::create_directories(parent, ec);
        }
        return path;
    }

    storage::Database db_;
};

// ===========================================================================
// Engine
// ===========================================================================
Engine::Engine(Config config)
    : impl_(std::make_unique<Impl>(config)), config_(std::move(config)) {}

Engine::~Engine() = default;

int Engine::recover_orphaned_runs() {
    storage::PipelineRunRepository runs(impl_->db_);
    storage::TaskRunRepository tasks(impl_->db_);

    int touched = 0;
    for (const char* stale_state : {"RUNNING", "CREATED"}) {
        for (const auto& run : runs.list_in_state(stale_state)) {
            runs.set_state(run.id, "INTERRUPTED");
            runs.mark_finished(run.id, "INTERRUPTED", now_ms());
            for (const auto& tr : tasks.list_for_run(run.id)) {
                const auto st = domain::parse_task_state(tr.state);
                if (!st || !domain::is_terminal(*st)) {
                    tasks.set_state(tr.id, "CANCELLED");
                    tasks.set_finished_at(tr.id, now_ms());
                }
            }
            ++touched;
        }
    }
    return touched;
}

Engine::RunReport Engine::run_pipeline(const domain::PipelineDefinition& pipeline,
                                       const RunRequest& request) {
    RunReport report;

    const bool cache_enabled = request.cache_enabled.value_or(config_.cache_enabled);
    const bool checksums = request.compute_checksums.value_or(config_.compute_checksums);
    const int concurrency = request.max_concurrency.value_or(
        pipeline.max_concurrency > 0 ? pipeline.max_concurrency : config_.max_concurrency);

    // --- validation ---
    resources::ResourcePool capacity_pool(config_.resource_capacity);
    validation::ValidationOptions vopts;
    vopts.check_executables = true;
    vopts.resource_pool = &capacity_pool;
    report.validation = validation::validate_pipeline(pipeline, vopts);
    if (!report.validation.ok()) {
        report.validation_failed = true;
        report.state = PipelineState::kFailed;
        return report;
    }

    // --- graph ---
    std::vector<std::string> nodes;
    nodes.reserve(pipeline.tasks.size());
    for (const auto& t : pipeline.tasks) {
        nodes.push_back(t.id);
    }
    std::vector<std::pair<std::string, std::string>> edges;
    edges.reserve(pipeline.edges.size());
    for (const auto& e : pipeline.edges) {
        edges.emplace_back(e.from, e.to);
    }
    auto built = dag::Dag::build(std::move(nodes), std::move(edges));
    if (!built.ok()) {
        for (const auto& de : built.errors) {
            report.validation.issues.push_back(
                {validation::Severity::kError, "", "dependencies", de.message});
        }
        report.validation_failed = true;
        report.state = PipelineState::kFailed;
        return report;
    }
    const dag::Dag& graph = *built.dag;

    // --- persist definition + run rows ---
    storage::Database& db = impl_->db_;
    const std::string doc_json = serialization::dump_pipeline(pipeline);
    const std::string doc_hash = util::sha256_hex(doc_json);

    storage::PipelineRepository pipelines(db);
    storage::PipelineRunRepository run_repo(db);
    storage::TaskRunRepository task_run_repo(db);

    const std::int64_t pipeline_id =
        pipelines.upsert(pipeline.name, doc_json, doc_hash, now_ms());
    const domain::RunId run_id =
        run_repo.create(pipeline_id, pipeline.name, concurrency, "CREATED", now_ms());
    report.run_id = run_id;

    std::unordered_map<std::string, std::int64_t> task_run_ids;
    for (const auto& t : pipeline.tasks) {
        task_run_ids[t.id] = task_run_repo.create(run_id, t.id, "PENDING");
    }

    // --- observers ---
    PersistenceObserver persistence(db, run_id, task_run_ids);
    FanoutObserver fanout;
    fanout.add(&persistence);
    fanout.add(request.observer);

    // --- scheduler ---
    scheduler::SchedulerConfig scfg;
    scfg.max_concurrency = concurrency;
    scfg.resource_capacity = config_.resource_capacity;
    scfg.verify_outputs = config_.verify_outputs;
    scfg.force_checksums = checksums;

    process::PosixProcessRunner runner;
    storage::CacheRepository cache_repo(db);
    cache::CacheStore cache_store(cache_repo, cache_enabled);

    scheduler::Scheduler sched(pipeline, graph, runner, scfg, &cache_store, &fanout);

    run_repo.mark_started(run_id, now_ms());

    // --- cross-process / signal cancellation watcher ---
    const fs::path sentinel = fs::path(config_.cancel_dir()) / std::to_string(run_id);
    std::atomic<bool> run_active{true};
    std::thread watcher([&] {
        while (run_active.load(std::memory_order_acquire)) {
            const bool ext = request.external_cancel != nullptr &&
                             request.external_cancel->load(std::memory_order_acquire);
            std::error_code ec;
            const bool file = fs::exists(sentinel, ec);
            if (ext || file) {
                sched.cancel();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
    });

    const auto result = sched.run();

    run_active.store(false, std::memory_order_release);
    watcher.join();

    std::error_code ec;
    fs::remove(sentinel, ec);

    run_repo.mark_finished(run_id, std::string(domain::to_string(result.state)), now_ms());

    report.state = result.state;
    report.run = result.run;
    return report;
}

bool Engine::request_cancel(domain::RunId run_id) {
    storage::PipelineRunRepository runs(impl_->db_);
    const auto rec = runs.get(run_id);
    if (!rec) {
        return false;
    }
    const auto st = domain::parse_pipeline_state(rec->state);
    if (st && domain::is_terminal(*st)) {
        return false;
    }
    std::error_code ec;
    fs::create_directories(config_.cancel_dir(), ec);
    std::ofstream(fs::path(config_.cancel_dir()) / std::to_string(run_id)) << "cancel\n";
    return true;
}

std::optional<Engine::RunView> Engine::get_run(domain::RunId id) {
    storage::PipelineRunRepository runs(impl_->db_);
    storage::TaskRunRepository task_runs(impl_->db_);
    storage::TaskAttemptRepository attempts(impl_->db_);

    const auto rec = runs.get(id);
    if (!rec) {
        return std::nullopt;
    }
    RunView view;
    view.id = rec->id;
    view.pipeline_name = rec->name;
    view.state = domain::parse_pipeline_state(rec->state).value_or(PipelineState::kCreated);
    view.created_at = rec->created_at;
    view.started_at = rec->started_at;
    view.finished_at = rec->finished_at;
    view.max_concurrency = rec->max_concurrency;

    for (const auto& tr : task_runs.list_for_run(id)) {
        TaskView tv;
        tv.task_id = tr.task_id;
        tv.state = domain::parse_task_state(tr.state).value_or(TaskState::kPending);
        tv.started_at = tr.started_at;
        tv.finished_at = tr.finished_at;
        tv.attempts = tr.attempts;
        const auto atts = attempts.list_for_task_run(tr.id);
        if (!atts.empty()) {
            tv.last_exit_code = atts.back().exit_code;
        }
        view.counts.add(tv.state);
        view.tasks.push_back(std::move(tv));
    }
    return view;
}

std::vector<Engine::RunView> Engine::list_runs(int limit) {
    storage::PipelineRunRepository runs(impl_->db_);
    std::vector<RunView> out;
    for (const auto& rec : runs.list_recent(limit)) {
        RunView view;
        view.id = rec.id;
        view.pipeline_name = rec.name;
        view.state = domain::parse_pipeline_state(rec.state).value_or(PipelineState::kCreated);
        view.created_at = rec.created_at;
        view.started_at = rec.started_at;
        view.finished_at = rec.finished_at;
        view.max_concurrency = rec.max_concurrency;
        out.push_back(std::move(view));
    }
    return out;
}

std::vector<storage::LogRecord> Engine::get_logs(domain::RunId id,
                                                 const std::optional<std::string>& task_id,
                                                 int limit) {
    storage::LogRepository logs(impl_->db_);
    return logs.query(id, task_id, limit);
}

int Engine::clear_cache() {
    storage::CacheRepository cache(impl_->db_);
    return cache.clear_all();
}

int Engine::prune_cache(int older_than_days) {
    storage::CacheRepository cache(impl_->db_);
    const std::int64_t cutoff =
        now_ms() - static_cast<std::int64_t>(older_than_days) * 24 * 60 * 60 * 1000;
    return cache.prune_older_than(cutoff);
}

std::int64_t Engine::cache_entry_count() {
    storage::CacheRepository cache(impl_->db_);
    return cache.count();
}

}  // namespace flowforge::engine
