# 0002 — Pipelines are an explicit DAG of tasks

## Context

Orchestrators need a dependency model. Options: implicit ordering (declaration
order), data-flow inference (wire outputs to inputs), or an explicit graph.

## Decision

A pipeline is an explicit Directed Acyclic Graph: named tasks plus `from → to`
dependency edges (with `depends_on` sugar). The graph is built once into an
immutable `dag::Dag` and frozen for the duration of a run.

## Consequences

- Dependencies are unambiguous and inspectable (`flowforge graph`).
- Cycle detection is a hard pre-run check with a diagnostic path
  (`a -> b -> c -> a`), not a runtime hang.
- The frozen graph lets the scheduler use integer-indexed adjacency and
  dependency counters (O(1) lookups, O(V+E) propagation).
- Mutating the graph (add/remove task/edge) is a build-time concern; you author
  a new pipeline rather than editing a running one.
- Data-flow is *declared* separately (artifacts) and not used to infer edges in
  v1 — explicit is safer, and inferred edges can be added later without
  changing the engine.
