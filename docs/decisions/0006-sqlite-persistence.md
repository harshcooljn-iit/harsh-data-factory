# 0006 — SQLite for persistence

## Context

Run history, per-attempt records, captured logs and the result cache need to
survive process exit and be queryable. Options: flat files / JSON per run, an
embedded KV store, an external database, or SQLite.

## Decision

A single SQLite database file (default `.flowforge/flowforge.sqlite`), WAL mode,
foreign keys on. Access is wrapped: RAII `Database` / `Statement` /
`Transaction`, a `PRAGMA user_version` migration runner, and repository classes
so no SQL string leaves `storage/`.

## Consequences

- Zero configuration, one file, transactional, good ad-hoc query story
  (`sqlite3 … "SELECT …"`).
- Prepared statements + typed binds everywhere; transactions for multi-row
  writes (log batches).
- One connection per engine, engine-thread only → the persistence observer
  needs no locking (scheduler callbacks are already serialised).
- Not built for concurrent writers / multiple engine processes — FlowForge
  assumes a single engine at a time and marks stale `RUNNING` runs
  `INTERRUPTED` on startup (see `docs/persistence.md`).
- Migrations are append-only and a newer-than-known schema is refused rather
  than mishandled.
