# Performance

## Design choices that matter

- **Dependency counters, not rescans.** Each task carries `remaining_deps`,
  seeded from `Dag::initial_dependency_counts()`. A completion decrements only
  its direct dependents. Dependency bookkeeping for a whole run is O(V + E).
- **Event-driven wait.** One condition variable; `wait` / `wait_until` on the
  nearest retry deadline. No polling, no periodic scans.
- **One reactor thread for all subprocesses.** N ready tasks add N pipe fds to
  one `poll(2)` set, not N threads.
- **Move semantics / const-correctness** through the value types; artifacts are
  file references, never file contents.

## Known costs (v0.1.0)

- `ReadyQueue` is a sorted `std::vector`: O(n) insert / remove. For very wide
  DAGs this shows as an O(n²) term. A heap + id index is the planned upgrade.
- The scheduler does a few O(V) scans per event (`earliest_retry_deadline`,
  `promote_backoff_ready`). Visible only on very deep chains of instant tasks;
  negligible next to real task runtimes.

## Measured results

Machine: **Apple M1, 8 cores, 8 GB RAM**, macOS 15, Apple Clang 17,
`Release` (`-O3`). Reproduce with:

```sh
cmake --preset release && cmake --build --preset release
./build/release/bin/bench_dag
./build/release/bin/bench_scheduler
```

Numbers are the median of the reported iterations.

### DAG build (build + cycle check + topological sort)

| Graph | Nodes | Edges | Median |
| --- | ---: | ---: | ---: |
| chain | 100 | 99 | 0.03 ms |
| chain | 1,000 | 999 | 0.33 ms |
| chain | 10,000 | 9,999 | 2.33 ms |
| layered | 100 | 900 | 0.09 ms |
| layered | 600 | 17,100 | 1.36 ms |
| layered | 1,400 | 93,100 | 8.15 ms |

`transitive_dependents` from the root of a 600-node / 17k-edge graph: **0.013 ms**.

### Scheduler overhead (instant tasks, `FakeProcessRunner`, pure orchestration)

| Shape | Size | Concurrency | Median |
| --- | --- | ---: | ---: |
| deep chain | 100 | 1 | 0.55 ms |
| deep chain | 1,000 | 1 | 6.6 ms |
| deep chain | 5,000 | 1 | 90 ms |
| wide fan-out/fan-in | 100 | 8 | 0.27 ms |
| wide fan-out/fan-in | 1,000 | 8 | 4.5 ms |
| wide fan-out/fan-in | 3,000 | 8 | 26 ms |

Interpretation: orchestrating a 1,000-task pipeline adds single-digit
milliseconds of engine overhead. Real workloads are dominated by the task
processes themselves.

### Behavioural concurrency

`examples/parallel_pipeline` (three ~1 s workers) with `--max-concurrency 3`
completes in **~1.06 s** vs ~3 s serial — the three subprocesses genuinely
overlap on the single reactor thread.

> These numbers are re-measured, not invented. If you change the scheduler or
> the DAG builder, re-run the benchmarks and update this table.
