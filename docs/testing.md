# Testing

Every test drives the same `flowforge_core` library the CLI uses.

```sh
ctest --preset debug                 # everything
ctest --preset debug -L unit         # fast, no child processes
ctest --preset debug -L integration  # real subprocesses + on-disk SQLite
ctest --preset release -L performance # benchmark-style
```

## Layout

| Dir | What | Label |
| --- | --- | --- |
| `tests/unit/` | pure logic; temp files only | `unit` |
| `tests/integration/` | spawns real helper programs, opens real databases, runs the built CLI binary | `integration` |
| `tests/performance/` | prints measured timings, asserts loose bounds | `performance` |
| `tests/helpers/` | tiny deterministic programs the integration tests shell out to (`emit`, `sleeper`, `flaky`, `transform`) | — |
| `tests/support/` | `FakeProcessRunner`, `PipelineBuilder`, `bench.hpp`, generated `helper_paths.hpp` | — |

## Coverage by subsystem

- **DAG** — node/edge queries, roots/leaves, deterministic topological order,
  dependency counts, transitive dependents, duplicate/dangling/self-edge/cycle
  diagnostics, 20k-node deep chain (no stack overflow).
- **Scheduler** (via `FakeProcessRunner`, real async shape, no fork) — linear /
  diamond / multi-root-multi-leaf, concurrency cap, priority ordering under
  scarcity, resource serialisation (4+4 fit, third waits), retry-to-success,
  retry exhaustion, downstream skip with independent branch continuing,
  transitive skip, non-retryable spawn failure, cancellation, log streaming.
- **Process runner** (real forks) — stdout/stderr capture, exit codes, spawn
  failure message, final partial line, working directory, timeout,
  SIGTERM→SIGKILL escalation, 12-way concurrency on one thread, cancellation.
- **Task state machine** — every legal transition accepted, illegal ones
  rejected.
- **Retries** — attempt accounting, exponential backoff with cap.
- **Caching** — key stability across env/input order, sensitivity to
  args/inputs, exclusion of name/priority/retry/resources; store→hit→
  input-change→miss; failed runs not cached; disabled store inert;
  missing-output invalidation.
- **Serialization** — valid parse, invalid JSON, missing required fields with
  location, unknown task type, unsupported schema version, dump round-trip.
- **Validation** — duplicate ids, dangling edge, cycle message, missing
  interpreter (task+field), PATH resolution, resource-pool fit, duplicate
  artifact-name warning, zero-CPU rejection.
- **Persistence** — idempotent migration, newer-schema rejection, hash-deduped
  pipeline upsert, run + task + attempt lifecycle, batched/filtered logs,
  cache touch/prune/clear, foreign-key enforcement.
- **Engine** (real processes) — persistence of a run, validation failure ⇒ no
  run, every retry attempt persisted, failure propagation + independent branch,
  cache hit on second run + bust on arg change, missing declared output fails
  the task, external-cancel, orphan recovery.
- **CLI** (built binary as a subprocess) — version/help/unknown-command exit
  codes, validation error surfacing, graph rendering, run + runs + status round
  trip, failing pipeline exit code, cache hit + clean-cache.
- **Examples** — all seven example pipelines executed end to end, including the
  CACHED-then-rerun cache behaviour.

## Parallelism test

`PosixProcessRunner.RunsManyProcessesConcurrentlyOnOneThread` and
`ExamplesTest.ParallelPipeline` launch several ~0.4–1 s sleeps and assert they
finish well under the serial time. These are **behavioural** concurrency
checks with generous upper bounds, not microbenchmarks, to stay CI-stable.

## Sanitizers

```sh
cmake --preset asan && cmake --build --preset asan && ctest --preset asan
```

Adds AddressSanitizer + UndefinedBehaviorSanitizer. The scheduler and process
runner (the concurrent code) are run under it as part of development.

## Not faked

Integration tests never fake process execution — they run real child
processes. Only unit-level scheduler tests use `FakeProcessRunner`, and even
that keeps the real threaded/callback shape.
