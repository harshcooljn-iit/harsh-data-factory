#include "flowforge/dag/dag.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace flowforge::dag {

std::string_view DagError::code_name() const noexcept {
    switch (code) {
        case Code::kDuplicateNode:
            return "duplicate_node";
        case Code::kUnknownEdgeEndpoint:
            return "unknown_edge_endpoint";
        case Code::kSelfDependency:
            return "self_dependency";
        case Code::kCycle:
            return "cycle";
    }
    return "unknown";
}

namespace {

// Iterative colour-DFS cycle finder. Returns the node-index sequence of a
// cycle (with the first node repeated at the end) or an empty vector when the
// graph is acyclic. Iterative so a 10k-deep chain cannot blow the stack.
std::vector<std::size_t> find_cycle(const std::vector<std::vector<std::size_t>>& succ) {
    enum Colour : std::uint8_t { kWhite, kGrey, kBlack };
    const std::size_t n = succ.size();
    std::vector<Colour> colour(n, kWhite);
    std::vector<std::size_t> parent(n, static_cast<std::size_t>(-1));

    // Explicit stack of (node, next-successor-cursor).
    std::vector<std::pair<std::size_t, std::size_t>> stack;

    for (std::size_t start = 0; start < n; ++start) {
        if (colour[start] != kWhite) {
            continue;
        }
        stack.emplace_back(start, 0);
        colour[start] = kGrey;

        while (!stack.empty()) {
            auto& [node, cursor] = stack.back();
            if (cursor < succ[node].size()) {
                const std::size_t next = succ[node][cursor];
                ++cursor;
                if (colour[next] == kWhite) {
                    colour[next] = kGrey;
                    parent[next] = node;
                    stack.emplace_back(next, 0);
                } else if (colour[next] == kGrey) {
                    // Back edge node -> next: reconstruct next ... node ... next.
                    std::vector<std::size_t> cycle;
                    std::size_t cur = node;
                    while (cur != next) {
                        cycle.push_back(cur);
                        cur = parent[cur];
                    }
                    cycle.push_back(next);
                    std::reverse(cycle.begin(), cycle.end());
                    cycle.push_back(next);
                    return cycle;
                }
            } else {
                colour[node] = kBlack;
                stack.pop_back();
            }
        }
    }
    return {};
}

}  // namespace

DagBuildResult Dag::build(std::vector<std::string> nodes,
                          const std::vector<std::pair<std::string, std::string>>& edges) {
    DagBuildResult result;
    Dag dag;
    dag.nodes_ = std::move(nodes);

    // --- unique ids ------------------------------------------------------
    std::unordered_map<std::string, std::size_t> index;
    index.reserve(dag.nodes_.size() * 2);
    for (std::size_t i = 0; i < dag.nodes_.size(); ++i) {
        const auto [it, inserted] = index.emplace(dag.nodes_[i], i);
        (void)it;
        if (!inserted) {
            result.errors.push_back({DagError::Code::kDuplicateNode,
                                     "duplicate task id '" + dag.nodes_[i] + "'",
                                     {}});
        }
    }
    if (!result.errors.empty()) {
        return result;  // index is unreliable, stop here
    }

    const std::size_t n = dag.nodes_.size();
    dag.succ_.assign(n, {});
    dag.pred_.assign(n, {});

    // --- edges ---------------------------------------------------------
    std::unordered_set<std::uint64_t> seen_edges;
    for (const auto& [from, to] : edges) {
        const auto fit = index.find(from);
        const auto tit = index.find(to);
        if (fit == index.end()) {
            result.errors.push_back({DagError::Code::kUnknownEdgeEndpoint,
                                     "dependency references unknown task '" + from + "'",
                                     {}});
            continue;
        }
        if (tit == index.end()) {
            result.errors.push_back({DagError::Code::kUnknownEdgeEndpoint,
                                     "dependency references unknown task '" + to + "'",
                                     {}});
            continue;
        }
        if (fit->second == tit->second) {
            result.errors.push_back({DagError::Code::kSelfDependency,
                                     "task '" + from + "' depends on itself",
                                     {}});
            continue;
        }
        const auto key = (static_cast<std::uint64_t>(fit->second) << 32) |
                         static_cast<std::uint64_t>(tit->second);
        if (!seen_edges.insert(key).second) {
            continue;  // duplicate edge: harmless, ignore
        }
        dag.succ_[fit->second].push_back(tit->second);
        dag.pred_[tit->second].push_back(fit->second);
    }
    if (!result.errors.empty()) {
        return result;
    }

    // --- cycles ------------------------------------------------------
    if (const auto cyc = find_cycle(dag.succ_); !cyc.empty()) {
        DagError err;
        err.code = DagError::Code::kCycle;
        err.cycle.reserve(cyc.size());
        for (const auto idx : cyc) {
            err.cycle.push_back(dag.nodes_[idx]);
        }
        std::string chain;
        for (std::size_t i = 0; i < err.cycle.size(); ++i) {
            if (i != 0) {
                chain += " -> ";
            }
            chain += err.cycle[i];
        }
        err.message = "pipeline contains a cycle: " + chain;
        result.errors.push_back(std::move(err));
        return result;
    }

    // --- derived structure --------------------------------------------
    dag.in_degree_.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        dag.in_degree_[i] = dag.pred_[i].size();
        if (dag.pred_[i].empty()) {
            dag.roots_.push_back(dag.nodes_[i]);
        }
        if (dag.succ_[i].empty()) {
            dag.leaves_.push_back(dag.nodes_[i]);
        }
    }

    // Kahn with a min-heap on node index => deterministic order.
    {
        std::vector<std::size_t> remaining = dag.in_degree_;
        std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<>> ready;
        for (std::size_t i = 0; i < n; ++i) {
            if (remaining[i] == 0) {
                ready.push(i);
            }
        }
        dag.topo_order_.reserve(n);
        while (!ready.empty()) {
            const std::size_t node = ready.top();
            ready.pop();
            dag.topo_order_.push_back(dag.nodes_[node]);
            for (const std::size_t nxt : dag.succ_[node]) {
                if (--remaining[nxt] == 0) {
                    ready.push(nxt);
                }
            }
        }
    }

    // --- owned id adjacency for the public accessors -----------------
    dag.pred_ids_.assign(n, {});
    dag.succ_ids_.assign(n, {});
    for (std::size_t i = 0; i < n; ++i) {
        dag.pred_ids_[i].reserve(dag.pred_[i].size());
        for (const std::size_t p : dag.pred_[i]) {
            dag.pred_ids_[i].push_back(dag.nodes_[p]);
        }
        dag.succ_ids_[i].reserve(dag.succ_[i].size());
        for (const std::size_t s : dag.succ_[i]) {
            dag.succ_ids_[i].push_back(dag.nodes_[s]);
        }
    }

    dag.index_ = std::move(index);
    result.dag = std::move(dag);
    return result;
}

bool Dag::contains(std::string_view id) const noexcept {
    return index_of(id).has_value();
}

std::optional<std::size_t> Dag::index_of(std::string_view id) const noexcept {
    const auto it = index_.find(std::string(id));
    if (it == index_.end()) {
        return std::nullopt;
    }
    return it->second;
}

const std::vector<std::string>& Dag::dependencies(std::string_view id) const {
    const auto idx = index_of(id);
    if (!idx) {
        static const std::vector<std::string> kEmpty;
        return kEmpty;
    }
    return pred_ids_[*idx];
}

const std::vector<std::string>& Dag::dependents(std::string_view id) const {
    const auto idx = index_of(id);
    if (!idx) {
        static const std::vector<std::string> kEmpty;
        return kEmpty;
    }
    return succ_ids_[*idx];
}

std::size_t Dag::in_degree(std::string_view id) const {
    const auto idx = index_of(id);
    return idx ? in_degree_[*idx] : 0;
}

std::size_t Dag::out_degree(std::string_view id) const {
    const auto idx = index_of(id);
    return idx ? succ_[*idx].size() : 0;
}

std::vector<std::string> Dag::transitive_dependents(std::string_view id) const {
    const auto start = index_of(id);
    if (!start) {
        return {};
    }
    std::vector<char> seen(nodes_.size(), 0);
    std::queue<std::size_t> frontier;
    frontier.push(*start);
    seen[*start] = 1;
    std::vector<std::size_t> hits;
    while (!frontier.empty()) {
        const std::size_t node = frontier.front();
        frontier.pop();
        for (const std::size_t nxt : succ_[node]) {
            if (!seen[nxt]) {
                seen[nxt] = 1;
                hits.push_back(nxt);
                frontier.push(nxt);
            }
        }
    }
    std::sort(hits.begin(), hits.end());
    std::vector<std::string> out;
    out.reserve(hits.size());
    for (const std::size_t h : hits) {
        out.push_back(nodes_[h]);
    }
    return out;
}

}  // namespace flowforge::dag
