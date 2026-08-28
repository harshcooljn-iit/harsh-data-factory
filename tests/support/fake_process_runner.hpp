#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "flowforge/process/process_runner.hpp"

namespace flowforge::test {

// Scripted outcome for one launched process.
struct FakeRun {
    std::vector<std::string> stdout_lines;
    std::vector<std::string> stderr_lines;
    int exit_code = 0;
    std::chrono::milliseconds duration{0};
    bool spawn_failure = false;
    std::string spawn_error;
};

// ---------------------------------------------------------------------------
// FakeProcessRunner -- an in-process ProcessRunner for deterministic scheduler
// tests. It keeps the real async shape (one worker thread, callbacks delivered
// off the caller thread) so the scheduler's event loop is genuinely exercised,
// but never forks anything.
//
// The behaviour function maps a ProcessSpec to a FakeRun. request_terminate
// makes an in-flight job finish early as cancelled/timed-out.
// ---------------------------------------------------------------------------
class FakeProcessRunner final : public process::ProcessRunner {
public:
    using Behavior = std::function<FakeRun(const process::ProcessSpec&)>;

    explicit FakeProcessRunner(Behavior behavior) : behavior_(std::move(behavior)) {
        worker_ = std::thread([this] { run(); });
    }

    ~FakeProcessRunner() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        worker_.join();
    }

    process::ProcessHandle launch(const process::ProcessSpec& spec,
                                  process::ProcessCallbacks callbacks) override {
        const auto handle = next_handle_.fetch_add(1);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            launched_programs_.push_back(spec.argv.empty() ? spec.program : spec.argv[0]);
            ++in_flight_;
            max_concurrent_ = std::max(max_concurrent_, in_flight_);
            jobs_.push_back(Job{handle, spec, std::move(callbacks), false, false});
        }
        cv_.notify_all();
        return handle;
    }

    void request_terminate(process::ProcessHandle handle, bool as_cancellation) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& job : jobs_) {
            if (job.handle == handle) {
                job.terminate = true;
                job.terminate_as_cancel = as_cancellation;
            }
        }
        terminate_flags_[handle] = as_cancellation;
    }

    std::size_t active_count() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return in_flight_;
    }

    // --- test observation helpers ---
    std::vector<std::string> launched_programs() {
        std::lock_guard<std::mutex> lock(mutex_);
        return launched_programs_;
    }
    std::size_t max_concurrent() {
        std::lock_guard<std::mutex> lock(mutex_);
        return max_concurrent_;
    }

private:
    struct Job {
        process::ProcessHandle handle;
        process::ProcessSpec spec;
        process::ProcessCallbacks callbacks;
        bool terminate;
        bool terminate_as_cancel;
    };

    void run() {
        while (true) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
                if (stop_ && jobs_.empty()) {
                    return;
                }
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }
            execute(job);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                --in_flight_;
            }
        }
    }

    void execute(Job& job) {
        const FakeRun script = behavior_(job.spec);

        process::ProcessResult result;
        result.pid = static_cast<std::int64_t>(1000 + job.handle);

        if (script.spawn_failure) {
            result.outcome = process::ProcessOutcome::kSpawnFailed;
            result.spawn_error =
                script.spawn_error.empty() ? "fake spawn failure" : script.spawn_error;
            if (job.callbacks.on_exit) {
                job.callbacks.on_exit(result);
            }
            return;
        }

        for (const auto& line : script.stdout_lines) {
            if (job.callbacks.on_stdout) {
                job.callbacks.on_stdout(line);
            }
        }
        for (const auto& line : script.stderr_lines) {
            if (job.callbacks.on_stderr) {
                job.callbacks.on_stderr(line);
            }
        }

        const auto start = std::chrono::steady_clock::now();
        const auto deadline = start + script.duration;
        bool cancelled = false;
        bool timed_out = false;
        while (std::chrono::steady_clock::now() < deadline) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                const auto it = terminate_flags_.find(job.handle);
                if (it != terminate_flags_.end()) {
                    cancelled = it->second;
                    timed_out = !it->second;
                    break;
                }
            }
            if (job.spec.timeout &&
                std::chrono::steady_clock::now() - start >= *job.spec.timeout) {
                timed_out = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        if (cancelled) {
            result.outcome = process::ProcessOutcome::kCancelled;
            result.term_signal = 15;
        } else if (timed_out) {
            result.outcome = process::ProcessOutcome::kTimedOut;
            result.term_signal = 15;
        } else {
            result.outcome = process::ProcessOutcome::kExited;
            result.exit_code = script.exit_code;
        }
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);

        if (job.callbacks.on_exit) {
            job.callbacks.on_exit(result);
        }
    }

    Behavior behavior_;
    std::thread worker_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Job> jobs_;
    std::unordered_map<process::ProcessHandle, bool> terminate_flags_;
    std::vector<std::string> launched_programs_;
    std::atomic<process::ProcessHandle> next_handle_{1};
    std::size_t in_flight_ = 0;
    std::size_t max_concurrent_ = 0;
    bool stop_ = false;
};

}  // namespace flowforge::test
