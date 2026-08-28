#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "flowforge/process/posix_process_runner.hpp"
#include "flowforge/process/process_runner.hpp"

#ifndef FLOWFORGE_CLI_BIN
#error "FLOWFORGE_CLI_BIN must be defined by the build"
#endif

namespace {

namespace fs = std::filesystem;
using namespace flowforge::process;

struct CliResult {
    int exit_code = -1;
    std::string out;
    std::string err;
    ProcessOutcome outcome = ProcessOutcome::kSpawnFailed;
};

struct CliTest : ::testing::Test {
    fs::path dir;
    void SetUp() override {
        dir = fs::temp_directory_path() /
              ("ff_cli_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::create_directories(dir);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    CliResult run(std::vector<std::string> args) {
        PosixProcessRunner runner;
        ProcessSpec spec;
        spec.program = FLOWFORGE_CLI_BIN;
        spec.argv.push_back(FLOWFORGE_CLI_BIN);
        for (auto& a : args) {
            spec.argv.push_back(std::move(a));
        }
        spec.working_directory = dir.string();
        CliResult r;
        const auto pr = run_blocking(runner, spec, &r.out, &r.err);
        r.exit_code = pr.exit_code;
        r.outcome = pr.outcome;
        return r;
    }

    void write(const std::string& name, std::string_view content) {
        std::ofstream(dir / name, std::ios::binary) << content;
    }
};

TEST_F(CliTest, VersionAndHelp) {
    const auto v = run({"version"});
    EXPECT_EQ(v.exit_code, 0);
    EXPECT_NE(v.out.find("FlowForge 0.1.0"), std::string::npos);

    const auto h = run({"--help"});
    EXPECT_EQ(h.exit_code, 0);
    EXPECT_NE(h.out.find("usage: flowforge"), std::string::npos);

    const auto u = run({"frobnicate"});
    EXPECT_EQ(u.exit_code, 2);
}

TEST_F(CliTest, ValidateReportsErrorsWithTaskAndField) {
    write("bad.json", R"({"name":"x","tasks":[
      {"id":"t","type":"python","interpreter":"/no/python","script":"nope.py"}]})");
    const auto r = run({"validate", "bad.json"});
    EXPECT_EQ(r.exit_code, 1);
    EXPECT_NE((r.err + r.out).find("Python interpreter not found"), std::string::npos);
}

TEST_F(CliTest, GraphRendersLevels) {
    write("p.json", R"({"name":"g","tasks":[
      {"id":"a","type":"executable","executable":"/bin/echo"},
      {"id":"b","type":"executable","executable":"/bin/echo"}],
      "dependencies":[{"from":"a","to":"b"}]})");
    const auto r = run({"graph", "p.json"});
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.out.find("Levels"), std::string::npos);
    EXPECT_NE(r.out.find("0  a"), std::string::npos);
}

TEST_F(CliTest, RunSucceedsAndIsQueryable) {
    write("p.json", R"({"name":"demo","tasks":[
      {"id":"a","type":"executable","executable":"/bin/echo","arguments":["hi"]},
      {"id":"b","type":"executable","executable":"/bin/echo","arguments":["bye"]}],
      "dependencies":[{"from":"a","to":"b"}]})");

    const auto run_res = run({"run", "p.json", "--plain", "--state-dir", ".ff"});
    EXPECT_EQ(run_res.exit_code, 0) << run_res.err;
    EXPECT_NE(run_res.out.find("Pipeline SUCCEEDED"), std::string::npos);

    const auto runs = run({"runs", "--state-dir", ".ff"});
    EXPECT_EQ(runs.exit_code, 0);
    EXPECT_NE(runs.out.find("demo"), std::string::npos);
    EXPECT_NE(runs.out.find("SUCCEEDED"), std::string::npos);

    const auto status = run({"status", "1", "--state-dir", ".ff"});
    EXPECT_EQ(status.exit_code, 0);
    EXPECT_NE(status.out.find("2 total"), std::string::npos) << status.out;
    EXPECT_NE(status.out.find("SUCCEEDED"), std::string::npos);
}

TEST_F(CliTest, RunFailingPipelineExitsNonZero) {
    write("p.json", R"({"name":"boom","tasks":[
      {"id":"a","type":"executable","executable":"/bin/sh","arguments":["-c","exit 3"]}]})");
    const auto r = run({"run", "p.json", "--plain", "--state-dir", ".ff"});
    EXPECT_EQ(r.exit_code, 1);
    EXPECT_NE(r.out.find("FAILED"), std::string::npos);
}

TEST_F(CliTest, CacheHitOnSecondRun) {
    write("p.json", R"({"name":"cached","tasks":[
      {"id":"gen","type":"executable","executable":"/bin/sh",
       "arguments":["-c","echo data > out.txt"],
       "outputs":[{"name":"out","path":"out.txt"}], "cache": true}]})");

    const auto first = run({"run", "p.json", "--plain", "--state-dir", ".ff"});
    EXPECT_EQ(first.exit_code, 0) << first.err;

    const auto second = run({"run", "p.json", "--plain", "--state-dir", ".ff"});
    EXPECT_EQ(second.exit_code, 0);
    EXPECT_NE(second.out.find("CACHED"), std::string::npos) << second.out;

    const auto clean = run({"clean-cache", "--state-dir", ".ff"});
    EXPECT_EQ(clean.exit_code, 0);
    EXPECT_NE(clean.out.find("removed"), std::string::npos);
}

}  // namespace
