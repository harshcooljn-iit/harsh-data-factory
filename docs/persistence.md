# Persistence

FlowForge keeps run history, logs and the result cache in a single SQLite
database (default `.flowforge/flowforge.sqlite`, WAL mode, foreign keys on).

## Access rules

- All SQL lives in `storage/`. Business logic uses repository classes and plain
  record structs — it never sees a SQL string or a `sqlite3_stmt`.
- One `Database` connection per `Engine`, used only from the engine/scheduler
  thread. `PersistenceObserver` runs on that thread (scheduler callbacks are
  serialised), so no additional locking is layered on.
- Writes that must be atomic use `storage::Transaction` (RAII BEGIN/COMMIT,
  ROLLBACK on scope exit without `commit()`). Log inserts are batched in one
  transaction.
- Every statement is prepared (`Database::prepare` → `Statement`), bound with
  typed setters, and finalised by RAII.

## Schema (migration 1)

| Table | Purpose | Notable columns / indexes |
| --- | --- | --- |
| `pipelines` | one row per distinct pipeline definition | `definition_hash` UNIQUE (SHA-256 of the canonical JSON) |
| `pipeline_runs` | one row per run | `state`, `created_at`, `started_at`, `finished_at`; indexes on `state` and `created_at DESC` |
| `task_runs` | one row per task per run | `UNIQUE(run_id, task_id)`, `state`, `ready_at`/`started_at`/`finished_at`, `attempts` |
| `task_attempts` | one row per execution attempt | `UNIQUE(task_run_id, attempt_number)`, `result_kind`, `exit_code`, `term_signal`, `pid` |
| `logs` | captured stdout/stderr/engine lines | `run_id`, `task_id`, `attempt_number`, `ts`, `severity`, `stream`, `message`; indexes on `run_id` and `(run_id, task_id)` |
| `cache_entries` | task-result cache | `cache_key` UNIQUE, `exit_code`, `outputs_json`, `last_used_at`, `hit_count` |
| `artifacts` | recorded artifact metadata | `run_id`, `task_id`, `role`, `logical_name`, `path`, `size_bytes`, `modified_unix_ms`, `checksum` |

Timestamps are `INTEGER` milliseconds since the Unix epoch.

## Migrations

`PRAGMA user_version` holds the schema version. `migrate_to_latest()`:

- runs every numbered migration after the current version inside one
  transaction, then sets `user_version`;
- is a no-op when already current;
- **refuses** a database whose `user_version` is newer than this build
  understands, rather than corrupting it.

Migrations are append-only. A shipped migration is never edited.

## Crash recovery model

FlowForge assumes **one engine process at a time**. It does not implement
distributed or multi-writer recovery.

On startup (`Engine::recover_orphaned_runs`, called by every DB-touching CLI
command), any run still in `RUNNING` or `CREATED` from a previous process is:

- marked `INTERRUPTED` with `finished_at = now`;
- its non-terminal `task_runs` marked `CANCELLED`.

This means an interrupted run is always identifiable in `flowforge runs` /
`flowforge status`; it is **not** resumed. Re-running the pipeline starts a
fresh run (and benefits from the result cache for tasks whose inputs are
unchanged).

## Cross-process cancellation

`flowforge cancel <run-id>` writes a sentinel file
`<state_dir>/cancel/<run-id>`. The engine running that pipeline has a watcher
thread that polls the sentinel (and an in-process atomic flag set by
SIGINT/SIGTERM) every 150 ms and calls `Scheduler::cancel()`. The sentinel is
removed when the run ends.

## Inspecting the database directly

```sh
sqlite3 .flowforge/flowforge.sqlite \
  "SELECT id, name, state, datetime(created_at/1000,'unixepoch') FROM pipeline_runs ORDER BY id DESC LIMIT 10;"
```
