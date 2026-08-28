# 0001 — CLI-first, engine as a library

## Context

FlowForge needs to be usable interactively, from scripts/CI, from tests, and
potentially from a future GUI or REST API. A GUI-first design tends to leak
presentation concerns into the core.

## Decision

The execution engine lives in a standalone static library, `flowforge_core`,
with no dependency on any CLI/presentation code. The `flowforge` binary is a
thin front end. The CLI, the unit tests and the integration tests all drive the
engine through the same public API (`engine::Engine`, `scheduler::Scheduler`).

## Consequences

- Every behaviour the CLI can trigger is reachable (and tested) through the
  library.
- Adding a REST API or TUI means adding a new front end + `SchedulerObserver`,
  not touching the scheduler.
- The build has an explicit `flowforge_core` target that tests link; there is
  no "test-only" reimplementation of engine logic.
- Slight ceremony: the CLI cannot reach into engine internals and must go
  through the API, which occasionally needs a new method.
