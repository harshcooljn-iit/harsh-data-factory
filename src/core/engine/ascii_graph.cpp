#include "flowforge/engine/ascii_graph.hpp"

#include <algorithm>
#include <sstream>
#include <vector>

namespace flowforge::engine {

namespace {

// Longest-path level for every node, computed over the topological order.
std::vector<int> compute_levels(const dag::Dag& graph) {
    std::vector<int> level(graph.size(), 0);
    for (const auto& id : graph.topological_order()) {
        const auto idx = graph.index_of(id);
        if (!idx) {
            continue;
        }
        for (const auto& dep : graph.dependents(id)) {
            const auto didx = graph.index_of(dep);
            if (didx) {
                level[*didx] = std::max(level[*didx], level[*idx] + 1);
            }
        }
    }
    return level;
}

void render_tree(const dag::Dag& graph, const std::string& node_id,
                 const std::string& line_prefix, bool is_root, bool is_last,
                 std::vector<char>& expanded, std::ostringstream& out) {
    const auto idx = graph.index_of(node_id);
    if (!idx) {
        return;
    }
    out << line_prefix;
    if (!is_root) {
        out << (is_last ? "`-- " : "|-- ");
    }
    out << node_id;

    if (expanded[*idx] != 0) {
        // Node already shown under another parent; don't repeat its subtree.
        out << " *\n";
        return;
    }
    out << '\n';
    expanded[*idx] = 1;
    const auto& children = graph.dependents(node_id);

    const std::string child_prefix =
        line_prefix + (is_root ? "" : (is_last ? "    " : "|   "));
    for (std::size_t i = 0; i < children.size(); ++i) {
        render_tree(graph, children[i], child_prefix, /*is_root=*/false,
                    i + 1 == children.size(), expanded, out);
    }
}

}  // namespace

std::string render_ascii_graph(const domain::PipelineDefinition& pipeline,
                               const dag::Dag& graph) {
    std::ostringstream out;
    out << "Pipeline: " << pipeline.name << "  (" << graph.size() << " tasks, "
        << pipeline.edges.size() << " dependencies)\n";

    if (graph.empty()) {
        out << "\n(empty pipeline)\n";
        return out.str();
    }

    // --- levels ---
    const auto levels = compute_levels(graph);
    const int max_level = *std::max_element(levels.begin(), levels.end());
    out << "\nLevels (tasks on the same level have no ordering constraint):\n";
    for (int lv = 0; lv <= max_level; ++lv) {
        out << "  " << lv << "  ";
        bool first = true;
        for (std::size_t i = 0; i < graph.size(); ++i) {
            if (levels[i] == lv) {
                if (!first) {
                    out << ", ";
                }
                out << graph.id_at(i);
                first = false;
            }
        }
        out << '\n';
    }

    // --- dependency tree ---
    out << "\nDependency tree (roots -> leaves, '*' = subtree shown above):\n";
    std::vector<char> expanded(graph.size(), 0);
    for (const auto& root : graph.roots()) {
        render_tree(graph, root, "", /*is_root=*/true, /*is_last=*/true, expanded, out);
    }

    return out.str();
}

}  // namespace flowforge::engine
