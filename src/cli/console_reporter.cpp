#include "console_reporter.hpp"

#include <cstdio>

#include "flowforge/util/string_utils.hpp"
#include "flowforge/util/time_utils.hpp"

namespace flowforge::cli {

using domain::TaskState;

ConsoleReporter::ConsoleReporter(Options options) : options_(options) {}

void ConsoleReporter::on_run_started(const domain::PipelineRun& run) {
    std::lock_guard<std::mutex> lock(mutex_);
    pipeline_name_ = run.pipeline_name;
    std::printf("Pipeline: %s  (%d tasks)\n\n", run.pipeline_name.c_str(),
                static_cast<int>(run.task_runs.size()));
    std::fflush(stdout);
}

void ConsoleReporter::print_status(std::string_view tag,
                                   std::string_view task_id,
                                   std::string extra) {
    if (options_.plain) {
        std::printf("%s  %-8.*s %.*s%s%s\n", util::to_iso8601(util::now()).c_str(),
                    static_cast<int>(tag.size()), tag.data(), static_cast<int>(task_id.size()),
                    task_id.data(), extra.empty() ? "" : "  ", extra.c_str());
    } else {
        std::printf("[%-8.*s] %.*s%s%s\n", static_cast<int>(tag.size()), tag.data(),
                    static_cast<int>(task_id.size()), task_id.data(),
                    extra.empty() ? "" : "  ", extra.c_str());
    }
    std::fflush(stdout);
}

void ConsoleReporter::on_task_state_changed(const domain::TaskRun& task, TaskState previous) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (previous == task.state) {
        return;
    }

    switch (task.state) {
        case TaskState::kRunning:
            print_status("RUNNING", task.task_id, "");
            break;
        case TaskState::kSucceeded:
            print_status("SUCCESS", task.task_id,
                         util::format_duration(task.duration_seconds()));
            break;
        case TaskState::kFailed: {
            std::string detail;
            if (const auto* a = task.last_attempt()) {
                detail = a->result.message;
            }
            print_status("FAILED", task.task_id, detail);
            break;
        }
        case TaskState::kCached:
            print_status("CACHED", task.task_id, "");
            break;
        case TaskState::kSkipped:
            print_status("SKIPPED", task.task_id, "upstream failed");
            break;
        case TaskState::kCancelled:
            print_status("CANCELLED", task.task_id, "");
            break;
        case TaskState::kReady:
            if (!task.attempts.empty()) {  // a retry is queued
                print_status("RETRY", task.task_id,
                             "attempt " + std::to_string(task.attempts_made() + 1));
            }
            break;
        case TaskState::kPending:
            break;
    }
}

void ConsoleReporter::on_task_log(std::string_view task_id,
                                  int attempt,
                                  std::string_view stream,
                                  std::string_view line) {
    if (options_.quiet || !options_.show_logs) {
        return;
    }
    (void)attempt;
    std::lock_guard<std::mutex> lock(mutex_);
    std::printf("    %.*s | %.*s: %.*s\n", static_cast<int>(task_id.size()), task_id.data(),
                static_cast<int>(stream.size()), stream.data(), static_cast<int>(line.size()),
                line.data());
    std::fflush(stdout);
}

void ConsoleReporter::on_run_finished(const domain::PipelineRun& run) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto counts = run.counts();
    std::printf("\nPipeline %s in %s\n", std::string(domain::to_string(run.state)).c_str(),
                util::format_duration(run.duration_seconds()).c_str());
    std::printf("  %d succeeded, %d cached, %d failed, %d skipped, %d cancelled  (of %d)\n",
                counts.succeeded, counts.cached, counts.failed, counts.skipped,
                counts.cancelled, counts.total);
    std::fflush(stdout);
}

}  // namespace flowforge::cli
