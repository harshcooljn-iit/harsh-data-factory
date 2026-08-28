#include <gtest/gtest.h>

#include <algorithm>

#include "flowforge/dag/dag.hpp"

namespace {

using flowforge::dag::Dag;
using flowforge::dag::DagError;
using Edges = std::vector<std::pair<std::string, std::string>>;

Dag build_ok(std::vector<std::string> nodes, Edges edges) {
    auto result = Dag::build(std::move(nodes), std::move(edges));
    EXPECT_TRUE(result.ok()) << (result.errors.empty() ? "" : result.errors.front().message);
    return std::move(*result.dag);
}

bool topo_precedes(const Dag& dag, const std::string& a, const std::string& b) {
    const auto& order = dag.topological_order();
    const auto ia = std::find(order.begin(), order.end(), a);
    const auto ib = std::find(order.begin(), order.end(), b);
    return ia < ib;
}

TEST(Dag, NodeCreationAndLookup) {
    const auto dag = build_ok({"a", "b", "c"}, {});
    EXPECT_EQ(dag.size(), 3u);
    EXPECT_TRUE(dag.contains("b"));
    EXPECT_FALSE(dag.contains("z"));
    EXPECT_EQ(dag.id_at(0), "a");
    EXPECT_EQ(dag.index_of("c").value(), 2u);
}

TEST(Dag, EdgesGiveDependenciesAndDependents) {
    // a -> b -> c , a -> c
    const auto dag = build_ok({"a", "b", "c"}, {{"a", "b"}, {"b", "c"}, {"a", "c"}});
    EXPECT_EQ(dag.dependencies("c").size(), 2u);
    EXPECT_EQ(dag.dependencies("a").size(), 0u);
    EXPECT_EQ(dag.dependents("a").size(), 2u);
    EXPECT_EQ(dag.in_degree("c"), 2u);
    EXPECT_EQ(dag.out_degree("a"), 2u);
}

TEST(Dag, RootsAndLeaves) {
    const auto dag = build_ok({"r1", "r2", "mid", "leaf"},
                              {{"r1", "mid"}, {"r2", "mid"}, {"mid", "leaf"}});
    EXPECT_EQ(dag.roots(), (std::vector<std::string>{"r1", "r2"}));
    EXPECT_EQ(dag.leaves(), (std::vector<std::string>{"leaf"}));
}

TEST(Dag, TopologicalOrderRespectsEdges) {
    const auto dag = build_ok({"prepare", "features", "stats", "train"},
                              {{"prepare", "features"},
                               {"prepare", "stats"},
                               {"features", "train"},
                               {"stats", "train"}});
    EXPECT_TRUE(topo_precedes(dag, "prepare", "features"));
    EXPECT_TRUE(topo_precedes(dag, "prepare", "stats"));
    EXPECT_TRUE(topo_precedes(dag, "features", "train"));
    EXPECT_TRUE(topo_precedes(dag, "stats", "train"));
}

TEST(Dag, TopologicalOrderIsDeterministicRegardlessOfEdgeOrder) {
    const auto a = build_ok({"a", "b", "c", "d"},
                            {{"a", "b"}, {"a", "c"}, {"b", "d"}, {"c", "d"}});
    const auto b = build_ok({"a", "b", "c", "d"},
                            {{"c", "d"}, {"b", "d"}, {"a", "c"}, {"a", "b"}});
    EXPECT_EQ(a.topological_order(), b.topological_order());
}

TEST(Dag, InitialDependencyCounts) {
    const auto dag = build_ok({"a", "b", "c"}, {{"a", "b"}, {"a", "c"}, {"b", "c"}});
    const auto& counts = dag.initial_dependency_counts();
    ASSERT_EQ(counts.size(), 3u);
    EXPECT_EQ(counts[dag.index_of("a").value()], 0u);
    EXPECT_EQ(counts[dag.index_of("b").value()], 1u);
    EXPECT_EQ(counts[dag.index_of("c").value()], 2u);
}

TEST(Dag, TransitiveDependents) {
    const auto dag = build_ok({"a", "b", "c", "d", "x"},
                              {{"a", "b"}, {"b", "c"}, {"b", "d"}, {"x", "d"}});
    EXPECT_EQ(dag.transitive_dependents("a"),
              (std::vector<std::string>{"b", "c", "d"}));
    EXPECT_TRUE(dag.transitive_dependents("c").empty());
}

TEST(Dag, DetectsDuplicateNodes) {
    const auto result = Dag::build({"a", "b", "a"}, {});
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.errors.front().code, DagError::Code::kDuplicateNode);
}

TEST(Dag, DetectsUnknownEdgeEndpoint) {
    const auto result = Dag::build({"a", "b"}, {{"a", "ghost"}});
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.errors.front().code, DagError::Code::kUnknownEdgeEndpoint);
}

TEST(Dag, DetectsSelfDependency) {
    const auto result = Dag::build({"a"}, {{"a", "a"}});
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.errors.front().code, DagError::Code::kSelfDependency);
}

TEST(Dag, DetectsCycleWithDiagnosticPath) {
    // a -> b -> c -> a
    const auto result = Dag::build({"a", "b", "c"}, {{"a", "b"}, {"b", "c"}, {"c", "a"}});
    ASSERT_FALSE(result.ok());
    const auto& err = result.errors.front();
    EXPECT_EQ(err.code, DagError::Code::kCycle);
    ASSERT_GE(err.cycle.size(), 2u);
    EXPECT_EQ(err.cycle.front(), err.cycle.back());  // closed loop
    EXPECT_NE(err.message.find(" -> "), std::string::npos);
}

TEST(Dag, AcyclicDiamondIsNotACycle) {
    const auto result = Dag::build({"a", "b", "c", "d"},
                                   {{"a", "b"}, {"a", "c"}, {"b", "d"}, {"c", "d"}});
    EXPECT_TRUE(result.ok());
}

TEST(Dag, DeepChainDoesNotOverflow) {
    std::vector<std::string> nodes;
    Edges edges;
    for (int i = 0; i < 20000; ++i) {
        nodes.push_back("n" + std::to_string(i));
        if (i > 0) {
            edges.push_back({"n" + std::to_string(i - 1), "n" + std::to_string(i)});
        }
    }
    const auto result = Dag::build(std::move(nodes), std::move(edges));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.dag->topological_order().size(), 20000u);
    EXPECT_EQ(result.dag->roots().size(), 1u);
}

}  // namespace
