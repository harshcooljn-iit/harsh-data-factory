#include "flowforge/storage/schema.hpp"

#include <array>
#include <string_view>

#include "flowforge/storage/database.hpp"

namespace flowforge::storage {

namespace {

// Migration N is applied when user_version < N. Keep each migration
// self-contained and append-only; never edit a shipped migration.
constexpr std::string_view kMigration1 = R"SQL(
CREATE TABLE pipelines (
    id              INTEGER PRIMARY KEY,
    name            TEXT    NOT NULL,
    definition_json TEXT    NOT NULL,
    definition_hash TEXT    NOT NULL UNIQUE,
    created_at      INTEGER NOT NULL
);

CREATE TABLE pipeline_runs (
    id              INTEGER PRIMARY KEY,
    pipeline_id     INTEGER NOT NULL REFERENCES pipelines(id),
    name            TEXT    NOT NULL,
    state           TEXT    NOT NULL,
    max_concurrency INTEGER NOT NULL,
    created_at      INTEGER NOT NULL,
    started_at      INTEGER,
    finished_at     INTEGER
);
CREATE INDEX idx_pipeline_runs_state   ON pipeline_runs(state);
CREATE INDEX idx_pipeline_runs_created ON pipeline_runs(created_at DESC);

CREATE TABLE task_runs (
    id           INTEGER PRIMARY KEY,
    run_id       INTEGER NOT NULL REFERENCES pipeline_runs(id),
    task_id      TEXT    NOT NULL,
    state        TEXT    NOT NULL,
    ready_at     INTEGER,
    started_at   INTEGER,
    finished_at  INTEGER,
    attempts     INTEGER NOT NULL DEFAULT 0,
    UNIQUE(run_id, task_id)
);
CREATE INDEX idx_task_runs_run ON task_runs(run_id);

CREATE TABLE task_attempts (
    id             INTEGER PRIMARY KEY,
    task_run_id    INTEGER NOT NULL REFERENCES task_runs(id),
    attempt_number INTEGER NOT NULL,
    state          TEXT    NOT NULL,
    result_kind    TEXT    NOT NULL,
    exit_code      INTEGER,
    term_signal    INTEGER,
    pid            INTEGER,
    started_at     INTEGER,
    finished_at    INTEGER,
    message        TEXT,
    UNIQUE(task_run_id, attempt_number)
);
CREATE INDEX idx_task_attempts_run ON task_attempts(task_run_id);

CREATE TABLE logs (
    id             INTEGER PRIMARY KEY,
    run_id         INTEGER NOT NULL REFERENCES pipeline_runs(id),
    task_id        TEXT,
    attempt_number INTEGER,
    ts             INTEGER NOT NULL,
    severity       TEXT    NOT NULL,
    stream         TEXT    NOT NULL,
    message        TEXT    NOT NULL
);
CREATE INDEX idx_logs_run      ON logs(run_id);
CREATE INDEX idx_logs_run_task ON logs(run_id, task_id);

CREATE TABLE cache_entries (
    id           INTEGER PRIMARY KEY,
    cache_key    TEXT    NOT NULL UNIQUE,
    task_id      TEXT    NOT NULL,
    exit_code    INTEGER NOT NULL,
    outputs_json TEXT    NOT NULL,
    created_at   INTEGER NOT NULL,
    last_used_at INTEGER NOT NULL,
    hit_count    INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_cache_entries_task ON cache_entries(task_id);

CREATE TABLE artifacts (
    id               INTEGER PRIMARY KEY,
    run_id           INTEGER NOT NULL REFERENCES pipeline_runs(id),
    task_id          TEXT    NOT NULL,
    role             TEXT    NOT NULL,
    logical_name     TEXT    NOT NULL,
    path             TEXT    NOT NULL,
    size_bytes       INTEGER,
    modified_unix_ms INTEGER,
    checksum         TEXT
);
CREATE INDEX idx_artifacts_run ON artifacts(run_id);
)SQL";

constexpr std::array<std::string_view, 1> kMigrations{kMigration1};

}  // namespace

void migrate_to_latest(Database& db) {
    const std::int64_t current = db.user_version();
    if (current > kCurrentSchemaVersion) {
        throw DatabaseError("schema",
                            "database schema version " + std::to_string(current) +
                                " is newer than this build supports (" +
                                std::to_string(kCurrentSchemaVersion) + ")",
                            0);
    }
    if (current == kCurrentSchemaVersion) {
        return;
    }

    Transaction tx(db);
    for (std::int64_t v = current; v < kCurrentSchemaVersion; ++v) {
        db.exec(kMigrations[static_cast<std::size_t>(v)]);
    }
    tx.commit();
    db.set_user_version(kCurrentSchemaVersion);
}

}  // namespace flowforge::storage
