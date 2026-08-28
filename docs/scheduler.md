# Scheduler

`scheduler::Scheduler` executes exactly one pipeline run to a terminal state.
It is the most important subsystem, so this document is detailed.

## Inputs

| Input | Notes |
| --- | --- |
| `PipelineDefinition` | immutable; outlives the scheduler |
| `Dag` | already built and validated (acyclic) by the caller |
| `ProcessRunner&` | how tasks actually run |
| `SchedulerConfig` | `max_concurrency`, `resource_capacity`, `verify_outputs`, `force_checksums` |
| `CacheStore*` | optional |
| `SchedulerObserver*` | optional; live output + persistence |
| `SchedulingPolicy*` | optional; defaults to priority / FIFO / id |

## State per task

Indexed by DAG node index (so every lookup is O(1)):

```
remaining_deps      dependency counter, seeded from Dag::initial_dependency_counts()
state               domain::TaskState
attempts_made       how many process attempts have started
handle              ProcessHandle of the in-flight attempt (if RUNNING)
retry_not_before    earliest time a retry may start
waiting_for_retry   true while sitting behind the backoff timer
reserved / has_reservation   the resources currently held for this task
```

Plus a `domain::PipelineRun` that accumulates the externally visible history
(`TaskRun` + `TaskAttempt` records with timestamps).

## The loop

```
seed: every task with remaining_deps == 0  ->  READY, pushed to the ready queue

repeat:
  if cancel requested and not yet cancelling:  begin_cancel()
  launch_phase()
  if all_settled():  break
  wait for an event   (wait_until the earliest retry deadline, else wait)
  drain the event batch:
     LogLine          -> observer.on_task_log
     ProcessFinished  -> handle_process_finished()
     Cancel           -> begin_cancel()
  promote_backoff_ready()      # move due retries into the ready queue

finalise: pipeline state = CANCELLED | FAILED | SUCCEEDED ; observer.on_run_finished
```

`all_settled()` = nothing running, ready queue empty, nobody waiting on a retry
backoff.

### launch_phase

Walk the ready queue in policy order. For each task, `try_start`:

1. **Probe inputs.** Any declared input missing → `MissingInput` failure
   (not retryable) → task `FAILED`, downstream skipped.
2. **Cache.** If enabled and the task opts in, compute the key from the probed
   inputs and look it up. A validated hit → task `CACHED`, dependents
   decremented, no process launched.
3. **Reserve resources.** `ResourcePool::try_reserve`. If it does not fit,
   leave the task in the queue and try the next one (backfill). A lower task
   that fits may start ahead of a blocked higher one.
4. **Launch.** Build a `ProcessSpec` from `TaskDefinition::resolve_command()`
   and `ArtifactManager::resolve_working_dir()`, hand it to the
   `ProcessRunner` with callbacks that push events. Task → `RUNNING`.

`launch_phase` re-scans after every start because a cache hit can make new
tasks ready mid-phase.

### handle_process_finished

```
running_count--
release the resource reservation
classify the ProcessResult  ->  TaskResult
if success and verify_outputs: a missing declared output downgrades it to MissingOutput
record the TaskAttempt (always persisted)

success        -> task SUCCEEDED ; store cache ; for each dependent: --remaining_deps,
                  enqueue when it hits zero
cancelled      -> task CANCELLED
other failure  -> on_task_failed()
```

### on_task_failed

```
if result.retryable() and retry budget remains:
    retry_not_before = now + RetryPolicy::delay_before(next_attempt)
    task -> READY (waiting_for_retry); promoted later by promote_backoff_ready()
else:
    task -> FAILED ; skip_downstream()
```

Retryable: non-zero exit, timeout, crash, missing declared output. **Not**
retryable: spawn failure (missing interpreter/executable), missing input,
cancellation.

### skip_downstream

Iterative DFS over dependents. A non-terminal, non-running dependent of a
failed/skipped task becomes `SKIPPED`; recurse. Independent branches are never
touched. See [`execution-model.md`](execution-model.md) for the exact table.

## Ready queue and scheduling policy

`ReadyQueue` keeps entries sorted best-first against the active
`SchedulingPolicy`, supports iteration (for backfill) and removal by id (for
cancellation).

`DefaultSchedulingPolicy` orders by:

1. **priority** — higher first
2. **readiness time** — earlier first (FIFO among equal priority)
3. **task id** — lexicographic, the deterministic final tie-break

Swap it by passing a `SchedulingPolicy*` to the `Scheduler` constructor.

Current `ReadyQueue` is a sorted `std::vector`: O(n) insert / remove. Fine for
the pipeline sizes FlowForge targets (see [`performance.md`](performance.md));
a heap + index is the obvious upgrade if needed.

## Concurrency & cancellation

- `max_concurrency` caps simultaneously `RUNNING` tasks.
- Resources are a second, independent gate (`ResourcePool`).
- `cancel()` is safe from any thread: it sets an atomic and pushes a `Cancel`
  event. On handling it, the scheduler stops launching, sends
  SIGTERM→SIGKILL to running tasks through the runner, and marks pending /
  backoff / ready tasks `CANCELLED`. The run ends once the running tasks report
  back.

## What the scheduler deliberately does **not** do

- No polling loop, no periodic full-DAG scan.
- No thread per task — one runner reactor thread handles every child.
- No knowledge of JSON, SQLite, or the CLI.
