#include "flowforge/storage/repositories.hpp"

namespace flowforge::storage {

// ===========================================================================
// PipelineRepository
// ===========================================================================
std::int64_t PipelineRepository::upsert(const std::string& name,
                                        const std::string& definition_json,
                                        const std::string& definition_hash,
                                        std::int64_t created_at) {
    {
        auto ins = db_.prepare(
            "INSERT INTO pipelines(name, definition_json, definition_hash, created_at) "
            "VALUES(?,?,?,?) ON CONFLICT(definition_hash) DO NOTHING;");
        ins.bind(1, name).bind(2, definition_json).bind(3, definition_hash).bind(4, created_at);
        ins.run();
    }
    auto sel = db_.prepare("SELECT id FROM pipelines WHERE definition_hash = ?;");
    sel.bind(1, definition_hash);
    if (!sel.step()) {
        throw DatabaseError("pipeline upsert", "row vanished after insert", 0);
    }
    return sel.column_int64(0);
}

std::optional<PipelineRecord> PipelineRepository::get(std::int64_t id) {
    auto s = db_.prepare(
        "SELECT id, name, definition_json, definition_hash, created_at FROM pipelines "
        "WHERE id = ?;");
    s.bind(1, id);
    if (!s.step()) {
        return std::nullopt;
    }
    PipelineRecord r;
    r.id = s.column_int64(0);
    r.name = s.column_text(1);
    r.definition_json = s.column_text(2);
    r.definition_hash = s.column_text(3);
    r.created_at = s.column_int64(4);
    return r;
}

// ===========================================================================
// PipelineRunRepository
// ===========================================================================
std::int64_t PipelineRunRepository::create(std::int64_t pipeline_id, const std::string& name,
                                           int max_concurrency, const std::string& state,
                                           std::int64_t created_at) {
    auto s = db_.prepare(
        "INSERT INTO pipeline_runs(pipeline_id, name, state, max_concurrency, created_at) "
        "VALUES(?,?,?,?,?);");
    s.bind(1, pipeline_id).bind(2, name).bind(3, state).bind(4, max_concurrency).bind(
        5, created_at);
    s.run();
    return db_.last_insert_rowid();
}

void PipelineRunRepository::set_state(std::int64_t id, const std::string& state) {
    auto s = db_.prepare("UPDATE pipeline_runs SET state = ? WHERE id = ?;");
    s.bind(1, state).bind(2, id);
    s.run();
}

void PipelineRunRepository::mark_started(std::int64_t id, std::int64_t ts) {
    auto s = db_.prepare(
        "UPDATE pipeline_runs SET state = 'RUNNING', started_at = ? WHERE id = ?;");
    s.bind(1, ts).bind(2, id);
    s.run();
}

void PipelineRunRepository::mark_finished(std::int64_t id, const std::string& state,
                                          std::int64_t ts) {
    auto s = db_.prepare(
        "UPDATE pipeline_runs SET state = ?, finished_at = ? WHERE id = ?;");
    s.bind(1, state).bind(2, ts).bind(3, id);
    s.run();
}

namespace {
PipelineRunRecord read_run(const Statement& s) {
    PipelineRunRecord r;
    r.id = s.column_int64(0);
    r.pipeline_id = s.column_int64(1);
    r.name = s.column_text(2);
    r.state = s.column_text(3);
    r.max_concurrency = static_cast<int>(s.column_int64(4));
    r.created_at = s.column_int64(5);
    r.started_at = s.column_opt_int64(6);
    r.finished_at = s.column_opt_int64(7);
    return r;
}
constexpr const char* kRunColumns =
    "id, pipeline_id, name, state, max_concurrency, created_at, started_at, finished_at";
}  // namespace

std::optional<PipelineRunRecord> PipelineRunRepository::get(std::int64_t id) {
    auto s = db_.prepare(std::string("SELECT ") + kRunColumns +
                         " FROM pipeline_runs WHERE id = ?;");
    s.bind(1, id);
    if (!s.step()) {
        return std::nullopt;
    }
    return read_run(s);
}

std::vector<PipelineRunRecord> PipelineRunRepository::list_recent(int limit) {
    auto s = db_.prepare(std::string("SELECT ") + kRunColumns +
                         " FROM pipeline_runs ORDER BY created_at DESC, id DESC LIMIT ?;");
    s.bind(1, limit);
    std::vector<PipelineRunRecord> out;
    while (s.step()) {
        out.push_back(read_run(s));
    }
    return out;
}

std::vector<PipelineRunRecord> PipelineRunRepository::list_in_state(const std::string& state) {
    auto s = db_.prepare(std::string("SELECT ") + kRunColumns +
                         " FROM pipeline_runs WHERE state = ? ORDER BY id;");
    s.bind(1, state);
    std::vector<PipelineRunRecord> out;
    while (s.step()) {
        out.push_back(read_run(s));
    }
    return out;
}

// ===========================================================================
// TaskRunRepository
// ===========================================================================
std::int64_t TaskRunRepository::create(std::int64_t run_id, const std::string& task_id,
                                       const std::string& state) {
    auto s = db_.prepare(
        "INSERT INTO task_runs(run_id, task_id, state) VALUES(?,?,?);");
    s.bind(1, run_id).bind(2, task_id).bind(3, state);
    s.run();
    return db_.last_insert_rowid();
}

void TaskRunRepository::set_state(std::int64_t id, const std::string& state) {
    auto s = db_.prepare("UPDATE task_runs SET state = ? WHERE id = ?;");
    s.bind(1, state).bind(2, id);
    s.run();
}

void TaskRunRepository::set_ready_at(std::int64_t id, std::int64_t ts) {
    auto s = db_.prepare("UPDATE task_runs SET ready_at = ? WHERE id = ?;");
    s.bind(1, ts).bind(2, id);
    s.run();
}

void TaskRunRepository::set_started_at(std::int64_t id, std::int64_t ts) {
    auto s = db_.prepare("UPDATE task_runs SET started_at = ? WHERE id = ?;");
    s.bind(1, ts).bind(2, id);
    s.run();
}

void TaskRunRepository::set_finished_at(std::int64_t id, std::int64_t ts) {
    auto s = db_.prepare("UPDATE task_runs SET finished_at = ? WHERE id = ?;");
    s.bind(1, ts).bind(2, id);
    s.run();
}

void TaskRunRepository::set_attempts(std::int64_t id, int attempts) {
    auto s = db_.prepare("UPDATE task_runs SET attempts = ? WHERE id = ?;");
    s.bind(1, attempts).bind(2, id);
    s.run();
}

std::vector<TaskRunRecord> TaskRunRepository::list_for_run(std::int64_t run_id) {
    auto s = db_.prepare(
        "SELECT id, run_id, task_id, state, ready_at, started_at, finished_at, attempts "
        "FROM task_runs WHERE run_id = ? ORDER BY id;");
    s.bind(1, run_id);
    std::vector<TaskRunRecord> out;
    while (s.step()) {
        TaskRunRecord r;
        r.id = s.column_int64(0);
        r.run_id = s.column_int64(1);
        r.task_id = s.column_text(2);
        r.state = s.column_text(3);
        r.ready_at = s.column_opt_int64(4);
        r.started_at = s.column_opt_int64(5);
        r.finished_at = s.column_opt_int64(6);
        r.attempts = static_cast<int>(s.column_int64(7));
        out.push_back(std::move(r));
    }
    return out;
}

// ===========================================================================
// TaskAttemptRepository
// ===========================================================================
std::int64_t TaskAttemptRepository::insert(const TaskAttemptRecord& rec) {
    auto s = db_.prepare(
        "INSERT INTO task_attempts(task_run_id, attempt_number, state, result_kind, "
        "exit_code, term_signal, pid, started_at, finished_at, message) "
        "VALUES(?,?,?,?,?,?,?,?,?,?);");
    s.bind(1, rec.task_run_id)
        .bind(2, rec.attempt_number)
        .bind(3, rec.state)
        .bind(4, rec.result_kind)
        .bind(5, rec.exit_code)
        .bind(6, rec.term_signal)
        .bind(7, rec.pid)
        .bind(8, rec.started_at)
        .bind(9, rec.finished_at)
        .bind(10, rec.message);
    s.run();
    return db_.last_insert_rowid();
}

std::vector<TaskAttemptRecord> TaskAttemptRepository::list_for_task_run(
    std::int64_t task_run_id) {
    auto s = db_.prepare(
        "SELECT id, task_run_id, attempt_number, state, result_kind, exit_code, term_signal, "
        "pid, started_at, finished_at, message FROM task_attempts WHERE task_run_id = ? "
        "ORDER BY attempt_number;");
    s.bind(1, task_run_id);
    std::vector<TaskAttemptRecord> out;
    while (s.step()) {
        TaskAttemptRecord r;
        r.id = s.column_int64(0);
        r.task_run_id = s.column_int64(1);
        r.attempt_number = static_cast<int>(s.column_int64(2));
        r.state = s.column_text(3);
        r.result_kind = s.column_text(4);
        r.exit_code = s.column_opt_int64(5);
        r.term_signal = s.column_opt_int64(6);
        r.pid = s.column_opt_int64(7);
        r.started_at = s.column_opt_int64(8);
        r.finished_at = s.column_opt_int64(9);
        r.message = s.column_opt_text(10);
        out.push_back(std::move(r));
    }
    return out;
}

// ===========================================================================
// LogRepository
// ===========================================================================
namespace {
void bind_log(Statement& s, const LogRecord& rec) {
    s.bind(1, rec.run_id)
        .bind(2, rec.task_id)
        .bind(3, rec.attempt_number)
        .bind(4, rec.ts)
        .bind(5, rec.severity)
        .bind(6, rec.stream)
        .bind(7, rec.message);
}
constexpr const char* kLogInsert =
    "INSERT INTO logs(run_id, task_id, attempt_number, ts, severity, stream, message) "
    "VALUES(?,?,?,?,?,?,?);";
}  // namespace

void LogRepository::insert(const LogRecord& rec) {
    auto s = db_.prepare(kLogInsert);
    bind_log(s, rec);
    s.run();
}

void LogRepository::insert_batch(const std::vector<LogRecord>& records) {
    if (records.empty()) {
        return;
    }
    Transaction tx(db_);
    auto s = db_.prepare(kLogInsert);
    for (const auto& rec : records) {
        bind_log(s, rec);
        s.run();
        s.reset();
    }
    tx.commit();
}

std::vector<LogRecord> LogRepository::query(std::int64_t run_id,
                                            const std::optional<std::string>& task_id,
                                            int limit) {
    std::string sql =
        "SELECT run_id, task_id, attempt_number, ts, severity, stream, message FROM logs "
        "WHERE run_id = ?";
    if (task_id) {
        sql += " AND task_id = ?";
    }
    sql += " ORDER BY ts, id LIMIT ?;";
    auto s = db_.prepare(sql);
    int idx = 1;
    s.bind(idx++, run_id);
    if (task_id) {
        s.bind(idx++, std::string_view(*task_id));
    }
    s.bind(idx, limit);

    std::vector<LogRecord> out;
    while (s.step()) {
        LogRecord r;
        r.run_id = s.column_int64(0);
        r.task_id = s.column_opt_text(1);
        r.attempt_number = s.column_opt_int64(2);
        r.ts = s.column_int64(3);
        r.severity = s.column_text(4);
        r.stream = s.column_text(5);
        r.message = s.column_text(6);
        out.push_back(std::move(r));
    }
    return out;
}

// ===========================================================================
// CacheRepository
// ===========================================================================
std::optional<CacheRecord> CacheRepository::lookup(const std::string& cache_key) {
    auto s = db_.prepare(
        "SELECT id, cache_key, task_id, exit_code, outputs_json, created_at, last_used_at, "
        "hit_count FROM cache_entries WHERE cache_key = ?;");
    s.bind(1, cache_key);
    if (!s.step()) {
        return std::nullopt;
    }
    CacheRecord r;
    r.id = s.column_int64(0);
    r.cache_key = s.column_text(1);
    r.task_id = s.column_text(2);
    r.exit_code = static_cast<int>(s.column_int64(3));
    r.outputs_json = s.column_text(4);
    r.created_at = s.column_int64(5);
    r.last_used_at = s.column_int64(6);
    r.hit_count = s.column_int64(7);
    return r;
}

void CacheRepository::store(const std::string& cache_key, const std::string& task_id,
                            int exit_code, const std::string& outputs_json, std::int64_t now) {
    auto s = db_.prepare(
        "INSERT INTO cache_entries(cache_key, task_id, exit_code, outputs_json, created_at, "
        "last_used_at, hit_count) VALUES(?,?,?,?,?,?,0) "
        "ON CONFLICT(cache_key) DO UPDATE SET exit_code = excluded.exit_code, "
        "outputs_json = excluded.outputs_json, last_used_at = excluded.last_used_at;");
    s.bind(1, cache_key)
        .bind(2, task_id)
        .bind(3, exit_code)
        .bind(4, outputs_json)
        .bind(5, now)
        .bind(6, now);
    s.run();
}

void CacheRepository::touch(const std::string& cache_key, std::int64_t now) {
    auto s = db_.prepare(
        "UPDATE cache_entries SET last_used_at = ?, hit_count = hit_count + 1 "
        "WHERE cache_key = ?;");
    s.bind(1, now).bind(2, cache_key);
    s.run();
}

int CacheRepository::clear_all() {
    db_.exec("DELETE FROM cache_entries;");
    return db_.changes();
}

int CacheRepository::prune_older_than(std::int64_t cutoff_ts) {
    auto s = db_.prepare("DELETE FROM cache_entries WHERE last_used_at < ?;");
    s.bind(1, cutoff_ts);
    s.run();
    return db_.changes();
}

std::int64_t CacheRepository::count() {
    auto s = db_.prepare("SELECT COUNT(*) FROM cache_entries;");
    if (!s.step()) {
        return 0;
    }
    return s.column_int64(0);
}

// ===========================================================================
// ArtifactRepository
// ===========================================================================
void ArtifactRepository::insert(const ArtifactRecord& rec) {
    auto s = db_.prepare(
        "INSERT INTO artifacts(run_id, task_id, role, logical_name, path, size_bytes, "
        "modified_unix_ms, checksum) VALUES(?,?,?,?,?,?,?,?);");
    s.bind(1, rec.run_id)
        .bind(2, rec.task_id)
        .bind(3, rec.role)
        .bind(4, rec.logical_name)
        .bind(5, rec.path)
        .bind(6, rec.size_bytes)
        .bind(7, rec.modified_unix_ms)
        .bind(8, rec.checksum);
    s.run();
}

std::vector<ArtifactRecord> ArtifactRepository::list_for_run(std::int64_t run_id) {
    auto s = db_.prepare(
        "SELECT run_id, task_id, role, logical_name, path, size_bytes, modified_unix_ms, "
        "checksum FROM artifacts WHERE run_id = ? ORDER BY id;");
    s.bind(1, run_id);
    std::vector<ArtifactRecord> out;
    while (s.step()) {
        ArtifactRecord r;
        r.run_id = s.column_int64(0);
        r.task_id = s.column_text(1);
        r.role = s.column_text(2);
        r.logical_name = s.column_text(3);
        r.path = s.column_text(4);
        r.size_bytes = s.column_opt_int64(5);
        r.modified_unix_ms = s.column_opt_int64(6);
        r.checksum = s.column_opt_text(7);
        out.push_back(std::move(r));
    }
    return out;
}

}  // namespace flowforge::storage
