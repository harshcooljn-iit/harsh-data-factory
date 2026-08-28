#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "flowforge/process/posix_process_runner.hpp"
#include "flowforge/process/process_runner.hpp"
#include "flowforge_test/helper_paths.hpp"

namespace {

using namespace flowforge::process;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

ProcessSpec spec_for(const std::string& program, std::vector<std::string> args) {
    ProcessSpec s;
    s.program = program;
    s.argv.push_back(program);
    for (auto& a : args) {
        s.argv.push_back(std::move(a));
    }
    s.working_directory = fs::temp_directory_path().string();
    return s;
}

TEST(PosixProcessRunner, CapturesStdoutStderrAndExitCode) {
    PosixProcessRunner runner;
    std::string out;
    std::string err;
    const auto result =
        run_blocking(runner,
                     spec_for(flowforge::test::kHelperEmit,
                              {"--stdout-lines", "3", "--stderr-lines", "2", "--exit", "7"}),
                     &out, &err);

    EXPECT_EQ(result.outcome, ProcessOutcome::kExited);
    EXPECT_EQ(result.exit_code, 7);
    EXPECT_TRUE(result.pid.has_value());
    EXPECT_EQ(std::count(out.begin(), out.end(), '\n'), 3);
    EXPECT_EQ(std::count(err.begin(), err.end(), '\n'), 2);
    EXPECT_NE(out.find("line out 0"), std::string::npos);
    EXPECT_NE(err.find("line err 1"), std::string::npos);
}

TEST(PosixProcessRunner, ReportsSpawnFailureForMissingExecutable) {
    PosixProcessRunner runner;
    const auto result = run_blocking(runner, spec_for("/nonexistent/flowforge/xyzzy", {}));
    EXPECT_EQ(result.outcome, ProcessOutcome::kSpawnFailed);
    EXPECT_FALSE(result.spawn_error.empty());
}

TEST(PosixProcessRunner, DeliversFinalPartialLineWithoutNewline) {
    PosixProcessRunner runner;
    std::string out;
    const auto result = run_blocking(
        runner,
        spec_for(flowforge::test::kHelperEmit, {"--stdout-lines", "2", "--no-newline"}), &out,
        nullptr);
    EXPECT_TRUE(result.succeeded());
    // run_blocking appends '\n' per delivered line; both lines must arrive.
    EXPECT_EQ(std::count(out.begin(), out.end(), '\n'), 2);
    EXPECT_NE(out.find("line out 1"), std::string::npos);
}

TEST(PosixProcessRunner, HonoursWorkingDirectory) {
    PosixProcessRunner runner;
    const auto sub = fs::temp_directory_path() / "flowforge_cwd_test";
    fs::create_directories(sub);
    const auto marker = sub / "made_here.txt";
    fs::remove(marker);

    ProcessSpec s = spec_for(flowforge::test::kHelperEmit, {"--write", "made_here.txt"});
    s.working_directory = sub.string();
    const auto result = run_blocking(runner, s);

    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(fs::exists(marker));
    fs::remove_all(sub);
}

TEST(PosixProcessRunner, TimeoutTerminatesAndReports) {
    PosixProcessRunner runner;
    ProcessSpec s = spec_for(flowforge::test::kHelperSleeper, {"5000"});
    s.timeout = 300ms;

    const auto start = std::chrono::steady_clock::now();
    const auto result = run_blocking(runner, s);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(result.outcome, ProcessOutcome::kTimedOut);
    EXPECT_LT(elapsed, 3s) << "timeout did not fire promptly";
}

TEST(PosixProcessRunner, TerminateEscalatesToKillForIgnoredSigterm) {
    PosixProcessRunner::Options opts;
    opts.terminate_grace = 200ms;
    PosixProcessRunner runner(opts);

    ProcessSpec s = spec_for(flowforge::test::kHelperSleeper, {"5000", "--ignore-term"});
    s.timeout = 200ms;
    const auto start = std::chrono::steady_clock::now();
    const auto result = run_blocking(runner, s);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(result.outcome, ProcessOutcome::kTimedOut);
    EXPECT_LT(elapsed, 3s);
}

TEST(PosixProcessRunner, RunsManyProcessesConcurrentlyOnOneThread) {
    PosixProcessRunner runner;
    constexpr int kN = 12;

    std::mutex m;
    int finished = 0;
    std::condition_variable cv;

    for (int i = 0; i < kN; ++i) {
        ProcessCallbacks cb;
        cb.on_exit = [&](const ProcessResult& r) {
            EXPECT_TRUE(r.succeeded());
            std::lock_guard<std::mutex> lock(m);
            ++finished;
            cv.notify_all();
        };
        const auto h =
            runner.launch(spec_for(flowforge::test::kHelperSleeper, {"400"}), std::move(cb));
        ASSERT_NE(h, kInvalidHandle);
    }

    const auto start = std::chrono::steady_clock::now();
    {
        std::unique_lock<std::mutex> lock(m);
        ASSERT_TRUE(cv.wait_for(lock, 10s, [&] { return finished == kN; }));
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    // 12 x 400ms sleeps finishing in well under the 4.8s serial time proves
    // they ran concurrently through the single reactor thread.
    EXPECT_LT(elapsed, 2s);
}

TEST(PosixProcessRunner, CancellationReportsCancelledOutcome) {
    PosixProcessRunner runner;
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    ProcessResult result;

    ProcessCallbacks cb;
    cb.on_exit = [&](const ProcessResult& r) {
        std::lock_guard<std::mutex> lock(m);
        result = r;
        done = true;
        cv.notify_all();
    };
    const auto h =
        runner.launch(spec_for(flowforge::test::kHelperSleeper, {"5000"}), std::move(cb));
    ASSERT_NE(h, kInvalidHandle);

    std::this_thread::sleep_for(150ms);
    runner.request_terminate(h, /*as_cancellation=*/true);

    std::unique_lock<std::mutex> lock(m);
    ASSERT_TRUE(cv.wait_for(lock, 5s, [&] { return done; }));
    EXPECT_EQ(result.outcome, ProcessOutcome::kCancelled);
}

}  // namespace
