# Error handling

FlowForge separates **user-facing errors** (concise, actionable) from
**diagnostic detail** (logs, verbose fields), and it never silently swallows a
failure.

## Error surfaces

| Layer | Mechanism | Consumed by |
| --- | --- | --- |
| Pipeline parsing | `serialization::LoadResult::errors` — `{location, message}` | CLI prints `location: message`; no run is created |
| Pre-run validation | `validation::ValidationReport` — `ValidationIssue{severity, task_id, field, message}` | CLI prints `error: task 'x' [field]: …`; run aborts on any `kError` |
| Graph structure | `dag::DagError` (`kDuplicateNode`, `kUnknownEdgeEndpoint`, `kSelfDependency`, `kCycle` with the path) | surfaced through validation |
| Process execution | `process::ProcessResult{outcome, exit_code, term_signal, spawn_error}` | classified by the scheduler |
| Task outcome | `domain::TaskResult{kind, exit_code, term_signal, message, detail}` + `retryable()` | scheduler decides retry / fail / skip; persisted per attempt |
| SQLite | `storage::DatabaseError` (context + SQLite detail + code), thrown | engine / CLI report it |

## `ResultKind` and retryability

| `ResultKind` | Meaning | Retryable? |
| --- | --- | --- |
| `Succeeded` | exit 0, outputs present | — |
| `FailedExit` | ran, exited non-zero | **yes** |
| `Timeout` | killed after exceeding `timeout_ms` | **yes** |
| `Crashed` | terminated by a signal | **yes** |
| `MissingOutput` | exit 0 but a declared output is absent | **yes** |
| `StartFailure` | could not spawn (missing interpreter / executable, bad cwd) | **no** |
| `MissingInput` | a declared input file was absent at launch | **no** |
| `Cancelled` | terminated because the run was cancelled | **no** |

Deterministic configuration problems are not retried no matter what the
`RetryPolicy` says.

## Message quality

Bad:

```
Execution failed.
```

What FlowForge produces:

```
error: task 'train' [interpreter]: Python interpreter not found: /bad/path/python3
```

```
[FAILED  ] feature_engineering  process exited with code 1
```

```
task exited 0 but declared output 'model.bin' was not produced
```

Each persisted `task_attempts` row keeps the `result_kind`, `exit_code`,
`term_signal`, `pid` and `message`, so `flowforge status` / `flowforge logs`
can answer: what failed, which task, which attempt, and why.

## Verbosity

- The CLI prints the concise error to stderr and a short summary to stdout.
- Full stdout/stderr of every task attempt is captured to the `logs` table and
  shown by `flowforge logs <run-id> [--task <id>] [--stream stderr]`.
- `flowforge run` streams task output live, prefixed by task id; `--quiet`
  suppresses the per-line stream but keeps status transitions.

## Exit codes

| Code | Meaning |
| --- | --- |
| `0` | success |
| `1` | ran but reported failure (validation errors, pipeline `FAILED`/`CANCELLED`, run not found) |
| `2` | usage error (unknown command, missing argument) |
