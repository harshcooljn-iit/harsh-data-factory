#pragma once

#include <string>

#include "flowforge/dag/dag.hpp"
#include "flowforge/domain/pipeline_definition.hpp"

namespace flowforge::engine {

// ---------------------------------------------------------------------------
// Deterministic textual rendering of a pipeline DAG for `flowforge graph`.
// Output has three sections:
//   * a header line (task / dependency counts)
//   * topological "levels" (tasks that can run in parallel share a level)
//   * an indented dependency tree rooted at each source task; a subtree shown
//     once is elided with " *" on later occurrences.
// Ordering everywhere follows DAG node (insertion) order, so the output is
// stable across runs.
// ---------------------------------------------------------------------------
[[nodiscard]] std::string render_ascii_graph(const domain::PipelineDefinition& pipeline,
                                             const dag::Dag& graph);

}  // namespace flowforge::engine
