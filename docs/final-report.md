# FlowForge — final report

Version 0.1.0. A local-first DAG pipeline orchestration engine: describe a graph
of tasks in JSON, each task is an external process, and FlowForge runs the
independent ones concurrently with retries, timeouts, cancellation, resource
limits, result caching and full run history in SQLite. CLI-first, no GUI, no
network, no distributed execution.

## Final architecture

A single static library `flowforge_core` holds the engine; the `flowforge`
binary is a thin front end over it. Modules, lowest to highest:

```
domain/ dag/ artifacts/ util/      value types, the frozen graph, no outside I/O
        │
storage/ (SQLite: Database, migrations, repositories — no SQL leaves here)
        │
resources/  process/  cache/  validation/  serialization/
        │
scheduler/  (Scheduler, ReadyQueue, SchedulingPolicy, SchedulerObserver)
        │
engine/  (Config, Engine: validate → persist → run → query; ascii_graph)
        │
src/cli/  (Args, ConsoleReporter, command dispatch)
```

See [`architecture.md`](architecture.md) for the diagram and the eight
invariants that are enforced by tests (core independent of CLI; scheduler
independent of task types; process management isolated; definition vs runtime
state; file artifacts; O(V+E) propagation; no busy-wait; no thread-per-task).

## Major components

| Component | What it does |
| --- | --- |
| `domain::PipelineDefinition` / `TaskDefinition` | immutable definition; no pid/attempt/RUNNING state |
| `dag::Dag` | frozen graph: O(1) lookup, cycle diagnostics with the path, deterministic topological order, dependency counts, transitive dependents |
| `validation::PipelineValidator` | pre-run checks that name the task + field |
| `process::ProcessRunner` / `PosixProcessRunner` | the only OS-process code; one reactor thread, `fork`+`execvp`, one `poll(2)` loop, SIGTERM→SIGKILL |
| `resources::ResourcePool` | reserve/release coarse cpu/mem/gpu counts |
| `artifacts::ArtifactManager` | resolve task file paths, `stat`, optional SHA-256 |
| `cache::CacheStore` | deterministic key + validated result cache |
| `storage/` | RAII SQLite wrapper, `user_version` migrations, repositories |
| `serialization/` | total JSON ↔ `PipelineDefinition` |
| `scheduler::Scheduler` | dependency-aware concurrent execution of one run |
| `engine::Engine` | config, run-with-persistence, run/log queries, orphan recovery, cross-process cancel |

## Scheduler algorithm

1. Seed: every task with in-degree 0 → `READY`.
2. Loop: launch phase, then (if not settled) `wait` / `wait_until(nearest
   retry deadline)` for an event, drain the event batch, promote due retries.
3. Launch phase walks the ready queue in policy order; per task: probe inputs →
   check cache → reserve resources (backfill if it doesn't fit) → launch.
4. On completion: release resources, classify the result, persist the attempt;
   success → `SUCCEEDED` + decrement dependents + maybe store cache; retryable
   failure with budget → `READY` behind a backoff timer; otherwise → `FAILED` +
   transitive `SKIPPED`.
5. Finalise: `CANCELLED` | `FAILED` | `SUCCEEDED`.

Dependency propagation is O(V + E) per run — counters, never a rescan. Full
detail in [`scheduler.md`](scheduler.md); exact semantics in
[`execution-model.md`](execution-model.md).

## Concurrency model

- The scheduler thread owns all scheduling state and the SQLite connection.
- One `PosixProcessRunner` reactor thread services every child; it communicates
  completions/log lines to the scheduler through one mutex-guarded event queue
  + condition variable.
- A watcher thread (spawned per run) polls an atomic cancel flag and a sentinel
  file and calls `Scheduler::cancel()`.
- `SchedulerObserver` callbacks all run on the scheduler thread in event order,
  so the SQLite-backed observer needs no locking.
- No thread per task; no polling; no giant mutex.

## Process execution model

`ProcessSpec` = program + argv vector + env + working dir + optional timeout.
No shell, ever. `PosixProcessRunner`: `fork`, then in the async-signal-safe
window `chdir` / `dup2` / `close` / set `environ`, then `execvp`; a
close-on-exec error pipe turns a failed exec into a precise `kSpawnFailed`
message. Stdout/stderr are drained line-by-line from one `poll` loop; exit is
seen as pipe EOF, confirmed with `waitpid`. Timeout/cancel escalate
SIGTERM→SIGKILL after a configurable grace period.

## Resource management

`ResourceRequirements{cpu_cores, memory_mb, gpu_count}` per task;
`ResourcePool` capacities. Reserve before launch, release on any terminal
state; reservations never exceed capacity; a stray double-release clamps at
zero. A task that can't fit an empty pool is a validation error. GPUs are
counted, not device-managed. See ADR 0010.

## Persistence

One SQLite file, WAL, foreign keys. Tables: `pipelines` (hash-deduped),
`pipeline_runs`, `task_runs`, `task_attempts` (every attempt), `logs`,
`cache_entries`, `artifacts` — with query-pattern indexes. `PRAGMA
user_version` migration runner (append-only, refuses a newer schema). One
connection, engine-thread only. Recovery: pre-existing `RUNNING`/`CREATED` runs
from a dead process become `INTERRUPTED` on startup — identifiable, not
resumed. Full detail in [`persistence.md`](persistence.md).

## Caching

Key = hash of (type, program, script, argv, sorted env, working dir, sorted
input identities). Excludes name/priority/retry/resources/timeout. Only exit-0
runs are stored. A hit is served only if every recorded output still validates
(checksum-match if checksummed, else size-match); a stale entry is deleted.
`first run → execute, second run → CACHED, input change → execute` is the
tested contract ([`caching.md`](caching.md), `examples/cache_pipeline`).

## Failure / retry semantics

Branch-local: a failed task `SKIP`s its transitive dependents; independent
branches run to completion; the pipeline ends `FAILED` if any task failed.
Retryable: non-zero exit, timeout, crash, missing declared output. Not
retryable: spawn failure, missing input, cancellation. Backoff before attempt
*k*: `min(max_delay, base_delay·multiplier^(k-2))`. Every attempt is persisted.
Table in [`execution-model.md`](execution-model.md).

## Cancellation

`Scheduler::cancel()` is thread-safe (atomic + event). It stops launching,
SIGTERM→SIGKILL running tasks via the runner, and marks pending/backoff/ready
tasks `CANCELLED`; the run ends once running tasks report back. `flowforge run`
wires SIGINT/SIGTERM to it; `flowforge cancel <id>` does it cross-process via a
sentinel file the engine's watcher polls. No unrelated processes are touched —
only pids FlowForge spawned.

## CLI design

`init`, `validate [--strict]`, `graph`, `run [--max-concurrency N] [--no-cache]
[--checksums] [--plain] [--quiet]`, `runs [--limit N]`, `status <id>`,
`logs <id> [--task] [--stream] [--limit]`, `cancel <id>`,
`clean-cache [--older-than-days N]`, `version`. Global `--state-dir`, `--db`,
`--config`. Exit codes: 0 ok / 1 reported failure / 2 usage. `run` output is
interactive by default, `--plain` (or non-tty) for CI. The CLI holds no
scheduling logic.

## Test coverage summary

**140 tests, all green** (`ctest --preset debug` + `-L performance`), also run
under ASan/UBSan.

| Lane | Count | Scope |
| --- | ---: | --- |
| unit | 107 | util, domain + state machine, DAG (incl. 20k-node chain), process spec, resources, artifacts, serialization, validation, storage, cache, ready queue, scheduler (13 scenarios via `FakeProcessRunner`), config, ascii graph |
| integration | 29 | real subprocesses: process runner (9), engine (8), CLI binary (6), all 7 example pipelines |
| performance | 4 | DAG build / scheduler overhead benchmarks |

Detail in [`testing.md`](testing.md). Integration tests never fake process
execution.

## Benchmark results (Apple M1, Release — re-measured, not invented)

| Case | Size | Median |
| --- | --- | ---: |
| DAG build+cycle+topo, chain | 10,000 nodes | 2.3 ms |
| DAG build, layered | 1,400 nodes / 93k edges | 8.2 ms |
| Scheduler overhead, deep chain | 1,000 tasks | 6.6 ms |
| Scheduler overhead, wide fan-out | 3,000 tasks, conc 8 | 26 ms |
| Behavioural concurrency | 3×1 s workers, conc 3 | ~1.06 s (vs ~3 s serial) |

Full table + method in [`performance.md`](performance.md).

## Known limitations

- **POSIX only.** macOS is the primary tested platform; Linux is expected to
  work and is in CI. **Windows is not supported** — the backend is behind
  `ProcessRunner` so a Win32 implementation can be added without touching the
  scheduler.
- **Single engine process.** No concurrent writers / multi-host coordination.
  Interrupted runs are marked, not resumed.
- **`ReadyQueue` is O(n)** insert/remove (sorted vector); shows as an O(n²)
  term on very wide DAGs. Heap + index is the planned upgrade.
- **Resource limits are nominal** — `cpu_cores` is a declared weight, not a
  cgroup/affinity limit. Memory defaults to a large ceiling.
- **No comments in the JSON format** (schema v1).
- GPU is an integer count with no device management.
- Cross-process cancel latency is up to the 150 ms watcher poll interval.

## Future improvements

Windows process backend · richer scheduling policies · heap-backed ready queue ·
real resource enforcement (cgroups / ulimit / GPU pinning) · container task
type · REST API / TUI front ends (via `SchedulerObserver`) · artifact
versioning · secret management · remote / distributed workers · resume of
interrupted runs · structured (JSON) log output.

## Notable engineering tradeoffs

- **Explicit DAG over inferred data-flow** — unambiguous, inspectable, enables
  counter-based scheduling. Data-flow edges can be inferred later.
- **One reactor thread over thread-per-task** — trivial synchronisation,
  scales to hundreds of fds; the reactor is a single I/O scheduling point,
  fine locally.
- **SQLite over flat files** — transactions + ad-hoc queries for free; not a
  multi-writer store, hence the single-engine assumption.
- **Validated cache over fast cache** — a `stat`/re-hash per output on lookup
  is the price of never serving a wrong result; failed runs are never cached.
- **Total parsing over exceptions** — every pipeline problem is a
  `{location, message}` the CLI can print; parse errors and semantic errors
  stay distinct.
- **Warnings-as-errors on the dev toolchain, off for release** — keeps the
  main compiler strict without letting a new compiler's pedantry block a
  release build.
