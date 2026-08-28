# 0005 — Dependency-counter, event-driven scheduler

## Context

A scheduler could re-scan the whole DAG each tick to find runnable tasks
("what has all deps done?"), poll for completions, and sleep-loop. That is
simple but wasteful and does not scale.

## Decision

- Each task holds a `remaining_deps` counter seeded from the DAG's in-degrees.
  A task becomes `READY` exactly when its counter hits zero. A completion
  decrements only its **direct** dependents. Propagation for a whole run is
  O(V + E).
- The scheduler has **one** wait loop on a condition variable. It `wait`s (or
  `wait_until`s the nearest retry-backoff deadline) and wakes on events pushed
  by the reactor thread or `cancel()`. No polling, no periodic full scans.
- Ready tasks are ordered by a replaceable `SchedulingPolicy`
  (priority → readiness time → id).
- Resource reservation is a second gate; the launch phase backfills a smaller
  task past a blocked larger one.

## Consequences

- Engine overhead for a 1,000-task pipeline is single-digit milliseconds
  (see `docs/performance.md`).
- Deterministic: same task outcomes ⇒ same states, same `SKIPPED` set, same
  final state.
- The current `ReadyQueue` is a sorted vector (O(n) ops); acceptable for target
  sizes, and swappable for a heap + index.
- A few O(V) scans per event (`earliest_retry_deadline`,
  `promote_backoff_ready`); measurable only on huge chains of instant tasks.
