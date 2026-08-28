#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "flowforge/engine/engine.hpp"
#include "flowforge/serialization/pipeline_document.hpp"

#ifndef FLOWFORGE_EXAMPLES_DIR
#error "FLOWFORGE_EXAMPLES_DIR must be defined by the build"
#endif

namespace {

namespace fs = std::filesystem;
using namespace flowforge;

struct ExamplesTest : ::testing::Test {
    fs::path examples{FLOWFORGE_EXAMPLES_DIR};
    fs::path state_root;
    void SetUp() override {
        state_root = fs::temp_directory_path() /
                     ("ff_examples_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::create_directories(state_root);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(state_root, ec);
    }

    engine::Config config_for(const std::string& name) {
        engine::Config c = engine::Config::defaults();
        c.state_dir = (state_root / name).string();
        return c;
    }

    engine::Engine::RunReport run(const std::string& name) {
        const auto file = examples / name / "pipeline.json";
        auto loaded = serialization::load_pipeline_from_file(file);
        EXPECT_TRUE(loaded.ok())
            << (loaded.errors.empty() ? file.string() : loaded.errors.front().to_string());
        engine::Engine eng(config_for(name));
        return eng.run_pipeline(*loaded.pipeline, {});
    }
};

TEST_F(ExamplesTest, Hello) {
    const auto r = run("hello");
    EXPECT_EQ(r.state, domain::PipelineState::kSucceeded);
    EXPECT_TRUE(fs::exists(examples / "hello" / "greeting.txt"));
}

TEST_F(ExamplesTest, PythonPipeline) {
    const auto r = run("python_pipeline");
    EXPECT_EQ(r.state, domain::PipelineState::kSucceeded) << "requires python3 on PATH";
    EXPECT_TRUE(fs::exists(examples / "python_pipeline" / "summary.txt"));
}

TEST_F(ExamplesTest, CppPipeline) {
    const auto r = run("cpp_pipeline");
    EXPECT_EQ(r.state, domain::PipelineState::kSucceeded);
    EXPECT_TRUE(fs::exists(examples / "cpp_pipeline" / "histogram.txt"));
}

TEST_F(ExamplesTest, ParallelPipeline) {
    const auto r = run("parallel_pipeline");
    EXPECT_EQ(r.state, domain::PipelineState::kSucceeded);
    EXPECT_EQ(r.run.counts().succeeded, 5);
}

TEST_F(ExamplesTest, FailurePipeline) {
    const auto r = run("failure_pipeline");
    EXPECT_EQ(r.state, domain::PipelineState::kFailed);
    EXPECT_EQ(r.run.task("good_branch")->state, domain::TaskState::kSucceeded);
    EXPECT_EQ(r.run.task("bad_branch")->state, domain::TaskState::kFailed);
    EXPECT_EQ(r.run.task("bad_child")->state, domain::TaskState::kSkipped);
}

TEST_F(ExamplesTest, RetryPipeline) {
    std::error_code ec;
    fs::remove(examples / "retry_pipeline" / "attempts.count", ec);
    const auto r = run("retry_pipeline");
    EXPECT_EQ(r.state, domain::PipelineState::kSucceeded);
    const auto* t = r.run.task("flaky");
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->attempts.size(), 3u);
}

TEST_F(ExamplesTest, CachePipelineReusesResultThenRerunsOnInputChange) {
    const auto data = examples / "cache_pipeline" / "data.in";
    {
        std::ofstream(data) << "cache example input v1\n";
    }

    const auto first = run("cache_pipeline");
    EXPECT_EQ(first.run.task("build")->state, domain::TaskState::kSucceeded);

    const auto second = run("cache_pipeline");
    EXPECT_EQ(second.run.task("build")->state, domain::TaskState::kCached);

    {
        std::ofstream(data) << "cache example input v2 CHANGED\n";
    }
    const auto third = run("cache_pipeline");
    EXPECT_EQ(third.run.task("build")->state, domain::TaskState::kSucceeded);
}

}  // namespace
