#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "flowforge/artifacts/artifact_manager.hpp"

namespace {

namespace fs = std::filesystem;
using flowforge::artifacts::ArtifactManager;
using flowforge::artifacts::probe_artifact;
using flowforge::domain::ArtifactDecl;
using flowforge::domain::TaskDefinition;

struct ArtifactsTest : ::testing::Test {
    fs::path root;
    void SetUp() override {
        root = fs::temp_directory_path() /
               ("ff_artifacts_" +
                std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
                std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::create_directories(root);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void write(const fs::path& rel, std::string_view content) {
        const auto p = root / rel;
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << content;
    }
};

TEST_F(ArtifactsTest, ProbeReportsSizeMtimeAndOptionalChecksum) {
    write("data/in.txt", "hello");
    const auto art = probe_artifact("in", root / "data/in.txt", /*want_checksum=*/true);
    EXPECT_TRUE(art.exists);
    EXPECT_EQ(art.size_bytes, 5);
    EXPECT_GT(art.modified_unix_ms, 0);
    ASSERT_TRUE(art.checksum.has_value());
    EXPECT_EQ(art.checksum->size(), 64u);

    const auto absent = probe_artifact("gone", root / "nope.txt", true);
    EXPECT_FALSE(absent.exists);
    EXPECT_FALSE(absent.checksum.has_value());
}

TEST_F(ArtifactsTest, IdentityChangesWithContentWhenChecksummed) {
    write("a.txt", "one");
    const auto first = probe_artifact("a", root / "a.txt", true);
    write("a.txt", "two");
    const auto second = probe_artifact("a", root / "a.txt", true);
    EXPECT_NE(first.identity(), second.identity());
}

TEST_F(ArtifactsTest, PathResolutionAgainstWorkingDir) {
    TaskDefinition task;
    task.id = "t";
    task.working_directory = "sub";
    ArtifactDecl decl{"out", "result.bin", false};

    ArtifactManager mgr(root);
    EXPECT_EQ(mgr.resolve_working_dir(task), (root / "sub").lexically_normal());
    EXPECT_EQ(mgr.resolve_artifact_path(task, decl),
              (root / "sub" / "result.bin").lexically_normal());
}

TEST_F(ArtifactsTest, AbsoluteArtifactPathUsedAsIs) {
    TaskDefinition task;
    task.working_directory = "sub";
    const auto abs = (root / "elsewhere.txt");
    ArtifactDecl decl{"x", abs.string(), false};
    ArtifactManager mgr(root);
    EXPECT_EQ(mgr.resolve_artifact_path(task, decl), abs.lexically_normal());
}

TEST_F(ArtifactsTest, MissingInputsAndOutputs) {
    write("work/present_in.txt", "x");
    TaskDefinition task;
    task.working_directory = "work";
    task.inputs = {{"present", "present_in.txt", false}, {"absent", "absent_in.txt", false}};
    task.outputs = {{"out", "out.bin", false}};

    ArtifactManager mgr(root);
    EXPECT_EQ(mgr.missing_inputs(task), (std::vector<std::string>{"absent"}));
    EXPECT_EQ(mgr.missing_outputs(task), (std::vector<std::string>{"out"}));

    write("work/out.bin", "done");
    EXPECT_TRUE(mgr.missing_outputs(task).empty());
}

}  // namespace
