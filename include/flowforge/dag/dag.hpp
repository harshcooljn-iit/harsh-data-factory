#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace flowforge::dag {

// ---------------------------------------------------------------------------
// DagError -- a structural problem found while building the graph. Cycle
// errors carry the offending node sequence so callers can print
// "A -> B -> C -> A" rather than a generic failure.
// ---------------------------------------------------------------------------
struct DagError {
    enum class Code {
        kDuplicateNode,
        kUnknownEdgeEndpoint,
        kSelfDependency,
        kCycle,
    };

    Code code{};
    std::string message;
    std::vector<std::string> cycle;  // populated only for Code::kCycle

    [[nodiscard]] std::string_view code_name() const noexcept;
};

// ---------------------------------------------------------------------------
// Dag -- an immutable directed acyclic graph over string task ids.
//
// Build it with Dag::build(); on success every query below is O(1) or
// O(degree) and the topological order is precomputed. Node indices follow
// insertion order, which makes every derived ordering deterministic and
// independent of edge insertion order.
//
// Mutation (add/remove task or edge) is a builder-time concern -- see
// DagBuilder -- because the scheduler relies on the graph being frozen.
// ---------------------------------------------------------------------------
// Forward declaration: DagBuildResult holds an std::optional<Dag> and so needs
// the complete Dag type; it is therefore defined after the class below.
struct DagBuildResult;

class Dag {
public:
    using BuildResult = DagBuildResult;

    // An incomplete return type is fine in a declaration; the definition of
    // DagBuildResult follows the class.
    [[nodiscard]] static DagBuildResult build(
        std::vector<std::string> nodes,
        const std::vector<std::pair<std::string, std::string>>& edges);

    [[nodiscard]] std::size_t size() const noexcept { return nodes_.size(); }
    [[nodiscard]] bool empty() const noexcept { return nodes_.empty(); }
    [[nodiscard]] const std::vector<std::string>& nodes() const noexcept { return nodes_; }

    [[nodiscard]] bool contains(std::string_view id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> index_of(std::string_view id) const noexcept;
    [[nodiscard]] const std::string& id_at(std::size_t idx) const { return nodes_.at(idx); }

    /// Direct predecessors ("what must finish before this task").
    [[nodiscard]] const std::vector<std::string>& dependencies(std::string_view id) const;
    /// Direct successors ("what becomes runnable when this task finishes").
    [[nodiscard]] const std::vector<std::string>& dependents(std::string_view id) const;

    /// Number of direct predecessors -- the initial dependency counter value.
    [[nodiscard]] std::size_t in_degree(std::string_view id) const;
    [[nodiscard]] std::size_t out_degree(std::string_view id) const;

    /// Nodes with no dependencies / no dependents, in insertion order.
    [[nodiscard]] const std::vector<std::string>& roots() const noexcept { return roots_; }
    [[nodiscard]] const std::vector<std::string>& leaves() const noexcept { return leaves_; }

    /// Deterministic topological ordering (Kahn, insertion-order tie-break).
    [[nodiscard]] const std::vector<std::string>& topological_order() const noexcept {
        return topo_order_;
    }

    /// Initial per-node dependency counts, indexed like nodes().
    [[nodiscard]] const std::vector<std::size_t>& initial_dependency_counts() const noexcept {
        return in_degree_;
    }

    /// All transitive dependents of @p id (everything downstream). Insertion order.
    [[nodiscard]] std::vector<std::string> transitive_dependents(std::string_view id) const;

private:
    Dag() = default;

    std::unordered_map<std::string, std::size_t> index_;
    std::vector<std::string> nodes_;
    std::vector<std::vector<std::size_t>> succ_;  // adjacency: index -> successor indices
    std::vector<std::vector<std::size_t>> pred_;
    std::vector<std::size_t> in_degree_;
    std::vector<std::string> roots_;
    std::vector<std::string> leaves_;
    std::vector<std::string> topo_order_;

    // String views into nodes_ would dangle on move; keep owned copies for the
    // dependency/dependent accessors.
    std::vector<std::vector<std::string>> pred_ids_;
    std::vector<std::vector<std::string>> succ_ids_;
};

struct DagBuildResult {
    std::optional<Dag> dag;
    std::vector<DagError> errors;
    [[nodiscard]] bool ok() const noexcept { return dag.has_value(); }
};

}  // namespace flowforge::dag
