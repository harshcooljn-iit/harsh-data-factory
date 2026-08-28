#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "flowforge/storage/database.hpp"

namespace flowforge::storage {

// ---------------------------------------------------------------------------
// Plain row records. Repositories own all SQL; business logic never sees a
// string of SQL or a sqlite3_stmt.
// ---------------------------------------------------------------------------
struct PipelineRecord {
    std::int64_t id = 0;
    std::string name;
    std::string definition_json;
    std::string definition_hash;
    std::int64_t created_at = 0;
};

struct PipelineRunRecord {
    std::int64_t id = 0;
    std::int64_t pipeline_id = 0;
    std::string name;
    std::string state;
    int max_concurrency = 0;
    std::int64_t created_at = 0;
    std::optional<std::int64_t> started_at;
    std::optional<std::int64_t> finished_at;
};

struct TaskRunRecord {
    std::int64_t id = 0;
    std::int64_t run_id = 0;
    std::string task_id;
    std::string state;
    std::optional<std::int64_t> ready_at;
    std::optional<std::int64_t> started_at;
    std::optional<std::int64_t> finished_at;
    int attempts = 0;
};

struct TaskAttemptRecord {
    std::int64_t id = 0;
    std::int64_t task_run_id = 0;
    int attempt_number = 1;
    std::string state;
    std::string result_kind;
    std::optional<std::int64_t> exit_code;
    std::optional<std::int64_t> term_signal;
    std::optional<std::int64_t> pid;
    std::optional<std::int64_t> started_at;
    std::optional<std::int64_t> finished_at;
    std::optional<std::string> message;
};

struct LogRecord {
    std::int64_t run_id = 0;
    std::optional<std::string> task_id;
    std::optional<std::int64_t> attempt_number;
    std::int64_t ts = 0;
    std::string severity;  // "info" | "warn" | "error"
    std::string stream;    // "stdout" | "stderr" | "engine"
    std::string message;
};

struct CacheRecord {
    std::int64_t id = 0;
    std::string cache_key;
    std::string task_id;
    int exit_code = 0;
    std::string outputs_json;
    std::int64_t created_at = 0;
    std::int64_t last_used_at = 0;
    std::int64_t hit_count = 0;
};

struct ArtifactRecord {
    std::int64_t run_id = 0;
    std::string task_id;
    std::string role;  // "input" | "output"
    std::string logical_name;
    std::string path;
    std::optional<std::int64_t> size_bytes;
    std::optional<std::int64_t> modified_unix_ms;
    std::optional<std::string> checksum;
};

// ---------------------------------------------------------------------------

class PipelineRepository {
  public:
    explicit PipelineRepository(Database& db) : db_(db) {}
    /// Insert the definition if its hash is new; return the row id either way.
    std::int64_t upsert(const std::string& name, const std::string& definition_json,
                        const std::string& definition_hash, std::int64_t created_at);
    std::optional<PipelineRecord> get(std::int64_t id);

  private:
    Database& db_;
};

class PipelineRunRepository {
  public:
    explicit PipelineRunRepository(Database& db) : db_(db) {}
    std::int64_t create(std::int64_t pipeline_id, const std::string& name, int max_concurrency,
                        const std::string& state, std::int64_t created_at);
    void set_state(std::int64_t id, const std::string& state);
    void mark_started(std::int64_t id, std::int64_t ts);
    void mark_finished(std::int64_t id, const std::string& state, std::int64_t ts);
    std::optional<PipelineRunRecord> get(std::int64_t id);
    std::vector<PipelineRunRecord> list_recent(int limit);
    std::vector<PipelineRunRecord> list_in_state(const std::string& state);

  private:
    Database& db_;
};

class TaskRunRepository {
  public:
    explicit TaskRunRepository(Database& db) : db_(db) {}
    std::int64_t create(std::int64_t run_id, const std::string& task_id,
                        const std::string& state);
    void set_state(std::int64_t id, const std::string& state);
    void set_ready_at(std::int64_t id, std::int64_t ts);
    void set_started_at(std::int64_t id, std::int64_t ts);
    void set_finished_at(std::int64_t id, std::int64_t ts);
    void set_attempts(std::int64_t id, int attempts);
    std::vector<TaskRunRecord> list_for_run(std::int64_t run_id);

  private:
    Database& db_;
};

class TaskAttemptRepository {
  public:
    explicit TaskAttemptRepository(Database& db) : db_(db) {}
    std::int64_t insert(const TaskAttemptRecord& rec);
    std::vector<TaskAttemptRecord> list_for_task_run(std::int64_t task_run_id);

  private:
    Database& db_;
};

class LogRepository {
  public:
    explicit LogRepository(Database& db) : db_(db) {}
    void insert(const LogRecord& rec);
    void insert_batch(const std::vector<LogRecord>& records);
    std::vector<LogRecord> query(std::int64_t run_id,
                                 const std::optional<std::string>& task_id, int limit);

  private:
    Database& db_;
};

class CacheRepository {
  public:
    explicit CacheRepository(Database& db) : db_(db) {}
    std::optional<CacheRecord> lookup(const std::string& cache_key);
    void store(const std::string& cache_key, const std::string& task_id, int exit_code,
               const std::string& outputs_json, std::int64_t now);
    void touch(const std::string& cache_key, std::int64_t now);
    int clear_all();
    int prune_older_than(std::int64_t cutoff_ts);
    std::int64_t count();

  private:
    Database& db_;
};

class ArtifactRepository {
  public:
    explicit ArtifactRepository(Database& db) : db_(db) {}
    void insert(const ArtifactRecord& rec);
    std::vector<ArtifactRecord> list_for_run(std::int64_t run_id);

  private:
    Database& db_;
};

}  // namespace flowforge::storage
