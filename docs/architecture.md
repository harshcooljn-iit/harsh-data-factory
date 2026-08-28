# Architecture

FlowForge is a local, CLI-first engine that runs a Directed Acyclic Graph of
tasks, where each task is an external process. This document describes the
layers and the rules that keep them separated.

## Layered view

```
                 ┌─────────────────────────┐
                 │        CLI (src/cli)     │   arg parsing, console output
                 └────────────┬────────────┘
                              │  engine::Engine API only
                 ┌────────────▼────────────┐
                 │   Application (engine/)  │   Config, run orchestration,
                 │                         │   persistence wiring, queries
                 └────────────┬────────────┘
                              │
      ┌───────────────────────┼─────────────────────────┐
      │                       │                         │
┌─────▼──────┐        ┌───────▼────────┐        ┌────────▼────────┐
│ validation │        │  scheduler/    │        │  serialization/ │
│            │        │  (the engine)  │        │  (JSON <-> dom) │
└─────┬──────┘        └───┬───┬───┬────┘        └─────────────────┘
      │                   │   │   │
      │        ┌──────────┘   │   └──────────┐
      │  ┌─────▼─────┐  ┌─────▼─────┐  ┌─────▼──────┐
      │  │ resources │  │  process  │  │   cache    │
      │  │  (pool)   │  │ (runner)  │  │            │
      │  └───────────┘  └───────────┘  └─────┬──────┘
      │                                      │
┌─────▼────────────────────────────────────────▼──────┐
│                    storage/ (SQLite)                 │
│      Database · schema · repositories               │
└─────────────────────────────────────────────────────┘
                              ▲
                     ┌────────┴────────┐
                     │   domain/       │   pure value types, no behaviour
                     │   dag/          │   that touches the outside world
                     │   artifacts/    │
                     │   util/         │
                     └─────────────────┘
```

Everything above `domain/` is in the single static library **`flowforge_core`**.
The CLI links only against that library. Unit and integration tests link the
same library. There is no code path the CLI exercises that the tests cannot.

## Modules

| Module | Responsibility | Key types |
| --- | --- | --- |
| `domain/` | Immutable pipeline definition + runtime state value types. No I/O. | `PipelineDefinition`, `TaskDefinition`, `RetryPolicy`, `ResourceRequirements`, `TaskState`/`PipelineState`, `PipelineRun`/`TaskRun`/`TaskAttempt`, `TaskResult` |
| `dag/` | Frozen directed acyclic graph over task ids. | `Dag` (build, cycle diagnostics, topological order, roots/leaves, dependency counts, transitive dependents) |
| `validation/` | Pre-run checks that name the offending task + field. | `PipelineValidator`, `ValidationReport` |
| `process/` | The *only* place OS process management lives. | `ProcessRunner` (interface), `PosixProcessRunner`, `ProcessSpec`, `ProcessResult` |
| `resources/` | Reserve / release coarse CPU / memory / GPU counts. | `ResourcePool` |
| `artifacts/` | Resolve task file paths, stat them, optional checksum. | `Artifact`, `ArtifactManager` |
| `cache/` | Deterministic cache key + validating result cache. | `compute_cache_key`, `CacheStore` |
| `storage/` | SQLite wrapper + migrations + repositories. No SQL escapes this module. | `Database`, `Statement`, `Transaction`, `migrate_to_latest`, `*Repository` |
| `serialization/` | JSON document <-> `PipelineDefinition`, total (never throws). | `load_pipeline_from_file`, `dump_pipeline` |
| `scheduler/` | Dependency-aware concurrent execution of one run. | `Scheduler`, `ReadyQueue`, `SchedulingPolicy`, `SchedulerObserver` |
| `engine/` | Application layer: config, run a pipeline with persistence, query history. | `Engine`, `Config`, `render_ascii_graph` |
| `src/cli/` | Presentation only. | `Args`, `ConsoleReporter`, command dispatch |

## Invariants (enforced by review and tests)

1. **The core engine does not depend on the CLI.** `flowforge_core` has no
   include of anything under `src/cli/`.
2. **The scheduler does not switch on concrete task types.** It resolves a
   `TaskDefinition` to a `ProcessSpec` and hands it to a `ProcessRunner`.
   Adding a task type touches `domain/enums`, `serialization/`, and
   `TaskDefinition::resolve_command()` only.
3. **Process management is isolated behind `ProcessRunner`.** Nothing above
   `process/` includes `<unistd.h>`, `<sys/wait.h>`, etc.
4. **Definition is separate from runtime state.** `TaskDefinition` has no pid,
   no attempt counter, no `RUNNING` flag; those live in `TaskRun` / `TaskAttempt`.
5. **Data flows through files.** The orchestrator tracks artifact metadata
   (path, size, mtime, optional checksum), never file contents.
6. **Dependency propagation is O(V + E) per run.** Counters, not rescans.
7. **No busy-waiting.** One condition variable, `wait` / `wait_until`.
8. **No thread per task.** One `ProcessRunner` reactor thread services every
   child; the scheduler adds no threads of its own.

## Threading model

| Thread | Owns | Talks to others via |
| --- | --- | --- |
| Engine / scheduler thread (the caller of `Scheduler::run()`) | all scheduling state, the SQLite connection, observer callbacks | reads the event queue |
| `PosixProcessRunner` reactor thread | child pids, pipe fds, timeouts | pushes completion / log events onto the scheduler event queue + `notify` |
| Cancellation watcher thread (spawned by `Engine::run_pipeline`) | nothing | calls `Scheduler::cancel()` (which just sets a flag + pushes an event) |
| Caller's signal handler | a `std::atomic<bool>` | the watcher polls it |

Because every `SchedulerObserver` callback runs on the scheduler thread in
event order, the `PersistenceObserver` uses the single SQLite connection with no
locking. See [`scheduler.md`](scheduler.md) and [`persistence.md`](persistence.md).

## Extension points that exist today

- **`SchedulingPolicy`** — swap the ready-queue ordering.
- **`ProcessRunner`** — a Windows backend, a container backend, or the
  in-process `FakeProcessRunner` used by tests.
- **`SchedulerObserver`** — additional live consumers (a TUI, a metrics sink)
  fan out alongside persistence.
- **Pipeline `schema_version`** — the loader is version-aware.
