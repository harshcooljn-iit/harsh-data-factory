#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

struct sqlite3;
struct sqlite3_stmt;

namespace flowforge::storage {

// ---------------------------------------------------------------------------
// DatabaseError -- thrown for any SQLite failure. Carries the failing SQL (or
// operation) plus the SQLite message so callers can report something better
// than "database error".
// ---------------------------------------------------------------------------
class DatabaseError : public std::runtime_error {
  public:
    DatabaseError(std::string context, std::string detail, int code)
        : std::runtime_error(context + ": " + detail),
          context_(std::move(context)),
          detail_(std::move(detail)),
          code_(code) {}

    [[nodiscard]] const std::string& context() const noexcept { return context_; }
    [[nodiscard]] const std::string& detail() const noexcept { return detail_; }
    [[nodiscard]] int code() const noexcept { return code_; }

  private:
    std::string context_;
    std::string detail_;
    int code_;
};

class Statement;

// ---------------------------------------------------------------------------
// Database -- RAII wrapper over a single sqlite3 connection. One connection is
// used per engine instance and is only ever touched from the engine thread, so
// no cross-thread locking is layered on top. Foreign keys and WAL are enabled
// on open.
// ---------------------------------------------------------------------------
class Database {
  public:
    /// Open (creating if needed) the database at @p path. ":memory:" is valid.
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&) noexcept;
    Database& operator=(Database&&) noexcept;

    /// Execute one or more statements with no result rows.
    void exec(std::string_view sql);

    /// Prepare a single statement for repeated binding / stepping.
    [[nodiscard]] Statement prepare(std::string_view sql);

    [[nodiscard]] std::int64_t last_insert_rowid() const;
    [[nodiscard]] int changes() const;

    [[nodiscard]] std::int64_t user_version();
    void set_user_version(std::int64_t version);

    [[nodiscard]] sqlite3* handle() const noexcept { return db_; }

  private:
    sqlite3* db_ = nullptr;
};

// ---------------------------------------------------------------------------
// Statement -- RAII wrapper over sqlite3_stmt. Bind indices are 1-based to
// match SQLite. step() returns true while a row is available.
// ---------------------------------------------------------------------------
class Statement {
  public:
    Statement() = default;
    Statement(sqlite3* db, sqlite3_stmt* stmt) : db_(db), stmt_(stmt) {}
    ~Statement();

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&&) noexcept;
    Statement& operator=(Statement&&) noexcept;

    Statement& bind(int index, std::int64_t value);
    Statement& bind(int index, int value) { return bind(index, static_cast<std::int64_t>(value)); }
    Statement& bind(int index, double value);
    Statement& bind(int index, std::string_view value);
    Statement& bind(int index, const std::string& value) {
        return bind(index, std::string_view(value));
    }
    Statement& bind(int index, const char* value) {
        return bind(index, std::string_view(value));
    }
    Statement& bind_null(int index);
    Statement& bind(int index, std::optional<std::int64_t> value);
    Statement& bind(int index, const std::optional<std::string>& value);

    /// Step once. Returns true if a row is available, false when done.
    [[nodiscard]] bool step();

    /// Step a statement expected to yield no rows.
    void run();

    void reset();

    [[nodiscard]] bool is_null(int column) const;
    [[nodiscard]] std::int64_t column_int64(int column) const;
    [[nodiscard]] double column_double(int column) const;
    [[nodiscard]] std::string column_text(int column) const;
    [[nodiscard]] std::optional<std::int64_t> column_opt_int64(int column) const;
    [[nodiscard]] std::optional<std::string> column_opt_text(int column) const;

  private:
    void check(int rc, const char* what) const;

    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
};

// ---------------------------------------------------------------------------
// Transaction -- RAII BEGIN/COMMIT. Rolls back if commit() is not called
// (e.g. an exception propagates).
// ---------------------------------------------------------------------------
class Transaction {
  public:
    explicit Transaction(Database& db);
    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit();

  private:
    Database& db_;
    bool active_ = true;
};

}  // namespace flowforge::storage
