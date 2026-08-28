#include <gtest/gtest.h>

#include "flowforge/engine/ascii_graph.hpp"
#include "pipeline_builder.hpp"

namespace {

using flowforge::engine::render_ascii_graph;
using flowforge::test::PipelineBuilder;

TEST(AsciiGraph, RendersLevelsAndTreeDeterministically) {
    PipelineBuilder b("ml_pipeline");
    b.task("prepare", "prepare")
        .task("features", "features")
        .task("stats", "stats")
        .task("train", "train")
        .edge("prepare", "features")
        .edge("prepare", "stats")
        .edge("features", "train")
        .edge("stats", "train");
    auto dag = b.build_dag();

    const std::string a = render_ascii_graph(b.pipeline(), dag);
    const std::string c = render_ascii_graph(b.pipeline(), dag);
    EXPECT_EQ(a, c);  // deterministic

    EXPECT_NE(a.find("Pipeline: ml_pipeline"), std::string::npos);
    EXPECT_NE(a.find("4 tasks, 4 dependencies"), std::string::npos);
    EXPECT_NE(a.find("0  prepare"), std::string::npos);
    EXPECT_NE(a.find("1  features, stats"), std::string::npos);
    EXPECT_NE(a.find("2  train"), std::string::npos);
    // train appears once fully, then elided with '*'
    EXPECT_NE(a.find("train *"), std::string::npos);
}

TEST(AsciiGraph, HandlesMultipleRoots) {
    PipelineBuilder b("multi");
    b.task("r1", "r1").task("r2", "r2").task("sink", "sink");
    b.edge("r1", "sink").edge("r2", "sink");
    auto dag = b.build_dag();
    const std::string out = render_ascii_graph(b.pipeline(), dag);
    EXPECT_NE(out.find("r1"), std::string::npos);
    EXPECT_NE(out.find("r2"), std::string::npos);
    EXPECT_NE(out.find("0  r1, r2"), std::string::npos);
}

TEST(AsciiGraph, HandlesSingleTask) {
    PipelineBuilder b("solo");
    b.task("only", "only");
    auto dag = b.build_dag();
    const std::string out = render_ascii_graph(b.pipeline(), dag);
    EXPECT_NE(out.find("1 tasks, 0 dependencies"), std::string::npos);
    EXPECT_NE(out.find("only"), std::string::npos);
}

}  // namespace
