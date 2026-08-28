# 0010 — Coarse reservation-based resource model

## Context

Two independent gates limit concurrency: a simple task count, and actual
machine resources (a 4-core task and another 4-core task should not both run on
an 8-core box alongside a third).

## Decision

- `ResourceRequirements { cpu_cores, memory_mb, gpu_count }` per task;
  `ResourcePool` with total capacities.
- The scheduler **reserves** a task's requirements before launching it and
  **releases** them on any terminal state (success, failure, cancellation,
  timeout). Reservations never exceed capacity.
- GPUs are an integer count only — accounted for, not device-managed.
- A task that cannot fit even an empty pool is a **validation error**
  (`can_ever_fit`), not something the scheduler waits on forever.
- The launch phase backfills: if the highest-priority ready task does not fit,
  a smaller one that does may start ahead of it.

## Consequences

- `A(4) + B(4)` run together on 8 cores; `C(4)` waits until one frees — proven
  by `Scheduler.ResourceAccountingSerialisesTasksThatDoNotFit`.
- `release()` clamps at zero, so a stray double-release cannot manufacture
  capacity.
- The model is nominal: `cpu_cores` is a declared weight, not a cgroup/affinity
  limit. Memory defaults to a large ceiling unless the pipeline lowers it.
- Real enforcement (cgroups, `ulimit`, GPU pinning) is future work behind the
  same reserve/release interface.
