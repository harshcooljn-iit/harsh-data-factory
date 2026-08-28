#pragma once

#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>

#include "flowforge/scheduler/scheduler.hpp"

namespace flowforge::cli {

// ---------------------------------------------------------------------------
// ConsoleReporter -- live `flowforge run` output.
//
//   interactive : status transitions with elapsed times, task log lines
//                 prefixed by task id.
//   plain (CI)  : one timestamped line per event, no cursor tricks.
//
// Callbacks arrive on the scheduler thread; a mutex guards stdout only.
// ---------------------------------------------------------------------------
class ConsoleReporter final : public scheduler::SchedulerObserver {
public:
    struct Options {
        bool plain = false;  ///< force CI-style output
        bool quiet = false;  ///< suppress per-task log lines
        bool show_logs = true;
    };

    explicit ConsoleReporter(Options options);

    void on_run_started(const domain::PipelineRun& run) override;
    void on_task_state_changed(const domain::TaskRun& task,
                               domain::TaskState previous) override;
    void on_task_log(std::string_view task_id,
                     int attempt,
                     std::string_view stream,
                     std::string_view line) override;
    void on_run_finished(const domain::PipelineRun& run) override;

private:
    void print_status(std::string_view tag, std::string_view task_id, std::string extra);

    Options options_;
    std::mutex mutex_;
    std::string pipeline_name_;
    std::unordered_map<std::string, domain::TaskState> last_state_;
};

}  // namespace flowforge::cli
