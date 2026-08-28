# Execution model & semantics

This document defines, precisely, what happens to a task and to the pipeline in
every situation. The scheduler is deterministic given the same task outcomes.

## Task states

```
PENDING ─▶ READY ─▶ RUNNING ─┬─▶ SUCCEEDED        (exit 0, outputs present)
                             ├─▶ FAILED           (retries exhausted / non-retryable)
                             ├─▶ CANCELLED        (run cancelled)
                             └─▶ READY            (retry: back off, then re-queue)
PENDING ─▶ SKIPPED                                (an upstream dep did not succeed)
READY   ─▶ CACHED                                 (valid cache hit)
PENDING/READY ─▶ CANCELLED                        (run cancelled before start)
```

Terminal: `SUCCEEDED`, `FAILED`, `SKIPPED`, `CANCELLED`, `CACHED`.
"Success-like" (a dependent may now proceed): `SUCCEEDED`, `CACHED`.

The full legal transition table is in `domain::is_valid_transition` and is
unit-tested exhaustively.

## Pipeline states

```
CREATED ─▶ RUNNING ─┬─▶ SUCCEEDED     (no task FAILED, not cancelled)
                    ├─▶ FAILED        (>= 1 task FAILED)
                    └─▶ CANCELLED     (cancellation requested and honoured)
RUNNING ─▶ INTERRUPTED               (assigned by crash recovery only)
```

## What happens when…

| Situation | Task result | Downstream | Pipeline |
| --- | --- | --- | --- |
| Process exits 0, declared outputs present | `SUCCEEDED` | each dependent's counter decremented; becomes `READY` at zero | continues |
| Process exits 0 but a declared output is missing (`verify_outputs`) | `FAILED` (`MissingOutput`, retryable) | after retries: `SKIPPED` transitively | `FAILED` |
| Process exits non-zero | retry if budget left, else `FAILED` (`FailedExit`) | on final `FAILED`: dependents `SKIPPED` | `FAILED` if any task `FAILED` |
| Process exceeds its timeout | SIGTERM→SIGKILL; `Timeout` (retryable) | as failure | as failure |
| Process killed by a signal | `Crashed` (retryable) | as failure | as failure |
| Interpreter / executable cannot be spawned | `FAILED` (`StartFailure`, **not** retryable) | `SKIPPED` transitively | `FAILED` |
| A declared input file is absent at launch | `FAILED` (`MissingInput`, **not** retryable) | `SKIPPED` transitively | `FAILED` |
| Valid cache hit | `CACHED` | treated exactly like `SUCCEEDED` | continues |
| A dependency ends `FAILED` / `SKIPPED` / `CANCELLED` | task never starts → `SKIPPED` | recursively `SKIPPED` | — |
| Run cancelled, task still `PENDING`/`READY`/backoff | `CANCELLED` | not started | `CANCELLED` |
| Run cancelled, task `RUNNING` | SIGTERM→SIGKILL → `CANCELLED` | not started | `CANCELLED` |
| Orchestrator process dies mid-run | run left `RUNNING` on disk | — | next engine start marks it `INTERRUPTED` |

## Failure propagation is branch-local

```
        A
       / \
      B   C
           \
            D
```

If **B** fails: `B → FAILED`. `C` and `D` are unaffected and run to completion.
The pipeline ends `FAILED` because a task failed, but unrelated work still
finished. FlowForge never aborts the whole pipeline the instant one branch
fails.

If instead **C** fails: `C → FAILED`, `D → SKIPPED` (its only dependency did
not succeed), `B` still runs.

## Retries

`RetryPolicy { max_retries, base_delay, backoff_multiplier, max_delay }`.

- Total attempts = `max_retries + 1`. Every attempt (including failures) is
  persisted as a `task_attempts` row.
- Backoff before attempt *k* (k ≥ 2):
  `min(max_delay, base_delay * backoff_multiplier^(k-2))`.
- Only retryable outcomes are retried (see the table). A deterministic
  configuration error is never retried, no matter the policy.
- While backing off, the task is `READY` with `waiting_for_retry` set; it does
  not occupy a concurrency slot or a resource reservation.

## Determinism

Given the same set of task outcomes (exit codes, timings bucketed by
completion order), the scheduler produces the same task states, the same
`SKIPPED` set, and the same final pipeline state. Ready-task ordering is fixed
by the scheduling policy with a task-id final tie-break.
