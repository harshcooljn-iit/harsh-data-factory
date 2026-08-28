#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "flowforge/domain/pipeline_definition.hpp"
#include "flowforge/domain/run_state.hpp"
#include "flowforge/engine/config.hpp"
#include "flowforge/scheduler/scheduler.hpp"
#include "flowforge/storage/repositories.hpp"
#include "flowforge/validation/pipeline_validator.hpp"

namespace flowforge::engine {

// ---------------------------------------------------------------------------
// Engine -- the application layer. Owns the SQLite connection and wires a
// pipeline run to persistence. Completely independent of the CLI: the CLI, the
// tests and any future front end use this same class.
// ---------------------------------------------------------------------------
class Engine {
  public:
    explicit Engine(Config config);
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    [[nodiscard]] const Config& config() const noexcept { return config_; }

    /// Mark runs left RUNNING by a previous, now-dead process as INTERRUPTED
    /// (and their unfinished task runs likewise). Returns the number of runs
    /// touched. FlowForge assumes a single engine process -- see
    /// docs/persistence.md.
    int recover_orphaned_runs();

    struct RunRequest {
        std::optional<int> max_concurrency;
        std::optional<bool> cache_enabled;
        std::optional<bool> compute_checksums;
        scheduler::SchedulerObserver* observer = nullptr;  ///< e.g. a console reporter
        std::atomic<bool>* external_cancel = nullptr;      ///< polled; set by a signal handler
    };

    struct RunReport {
        bool validation_failed = false;
        validation::ValidationReport validation;
        domain::RunId run_id = domain::kInvalidRunId;
        domain::PipelineState state = domain::PipelineState::kCreated;
        domain::PipelineRun run;
    };

    /// Validate, persist and execute @p pipeline. On validation failure no run
    /// row is created and RunReport::validation_failed is set.
    RunReport run_pipeline(const domain::PipelineDefinition& pipeline, const RunRequest& request);

    /// Create a cancellation sentinel for a run executing in another process.
    /// Returns false if the run is unknown or already finished.
    bool request_cancel(domain::RunId run_id);

    // ---- queries -----------------------------------------------------
    struct TaskView {
        std::string task_id;
        domain::TaskState state = domain::TaskState::kPending;
        std::optional<std::int64_t> started_at;
        std::optional<std::int64_t> finished_at;
        int attempts = 0;
        std::optional<std::int64_t> last_exit_code;
    };

    struct RunView {
        domain::RunId id = domain::kInvalidRunId;
        std::string pipeline_name;
        domain::PipelineState state = domain::PipelineState::kCreated;
        std::int64_t created_at = 0;
        std::optional<std::int64_t> started_at;
        std::optional<std::int64_t> finished_at;
        int max_concurrency = 0;
        domain::RunCounts counts;
        std::vector<TaskView> tasks;  // empty for list_runs()
    };

    [[nodiscard]] std::optional<RunView> get_run(domain::RunId id);
    [[nodiscard]] std::vector<RunView> list_runs(int limit);
    [[nodiscard]] std::vector<storage::LogRecord> get_logs(
        domain::RunId id, const std::optional<std::string>& task_id, int limit);

    int clear_cache();
    int prune_cache(int older_than_days);
    [[nodiscard]] std::int64_t cache_entry_count();

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    Config config_;
};

}  // namespace flowforge::engine
