#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "bench.hpp"
#include "flowforge/dag/dag.hpp"

namespace {

using flowforge::dag::Dag;
using flowforge::test::bench;
using Edges = std::vector<std::pair<std::string, std::string>>;

// A "layered" DAG: `layers` layers of `width` nodes, every node connected to
// every node in the next layer. Nodes = layers*width, edges ~ (layers-1)*width^2.
struct LayeredGraph {
    std::vector<std::string> nodes;
    Edges edges;
};

LayeredGraph make_layered(int layers, int width) {
    LayeredGraph g;
    g.nodes.reserve(static_cast<std::size_t>(layers) * width);
    for (int l = 0; l < layers; ++l) {
        for (int w = 0; w < width; ++w) {
            g.nodes.push_back("l" + std::to_string(l) + "_" + std::to_string(w));
        }
    }
    for (int l = 0; l + 1 < layers; ++l) {
        for (int a = 0; a < width; ++a) {
            for (int b = 0; b < width; ++b) {
                g.edges.emplace_back("l" + std::to_string(l) + "_" + std::to_string(a),
                                     "l" + std::to_string(l + 1) + "_" + std::to_string(b));
            }
        }
    }
    return g;
}

std::pair<std::vector<std::string>, Edges> make_chain(int n) {
    std::vector<std::string> nodes;
    Edges edges;
    for (int i = 0; i < n; ++i) {
        nodes.push_back("n" + std::to_string(i));
        if (i > 0) {
            edges.emplace_back("n" + std::to_string(i - 1), "n" + std::to_string(i));
        }
    }
    return {std::move(nodes), std::move(edges)};
}

TEST(BenchDag, BuildCycleTopoAcrossSizes) {
    std::printf("\n[DAG build + cycle-check + topological sort]\n");
    for (const int n : {100, 1000, 10000}) {
        auto [nodes, edges] = make_chain(n);
        const double ms = bench(
            "chain n=" + std::to_string(n), 20, [&] {
                auto r = Dag::build(nodes, edges);
                ASSERT_TRUE(r.ok());
                ASSERT_EQ(r.dag->topological_order().size(), static_cast<std::size_t>(n));
            });
        EXPECT_LT(ms, 250.0) << "chain build regressed badly at n=" << n;
    }

    for (const auto [layers, width] : {std::pair{10, 10}, std::pair{20, 30}, std::pair{20, 70}}) {
        auto g = make_layered(layers, width);
        const std::size_t node_count = g.nodes.size();
        const std::size_t edge_count = g.edges.size();
        bench("layered nodes=" + std::to_string(node_count) + " edges=" +
                  std::to_string(edge_count),
              10, [&] {
                  auto r = Dag::build(g.nodes, g.edges);
                  ASSERT_TRUE(r.ok());
              });
    }
}

TEST(BenchDag, TransitiveDependentsWideGraph) {
    std::printf("\n[transitive_dependents on a wide layered graph]\n");
    auto g = make_layered(15, 40);  // 600 nodes
    auto r = Dag::build(g.nodes, g.edges);
    ASSERT_TRUE(r.ok());
    const Dag& dag = *r.dag;
    bench("transitive_dependents(root)", 200,
          [&] { volatile auto v = dag.transitive_dependents(dag.roots().front()).size(); (void)v; });
}

}  // namespace
