#include "flowforge/storage/database.hpp"

#include <sqlite3.h>

#include <utility>

namespace flowforge::storage {

namespace {
[[noreturn]] void fail(sqlite3* db, const char* what, int rc) {
    const char* msg = db != nullptr ? sqlite3_errmsg(db) : sqlite3_errstr(rc);
    throw DatabaseError(what, msg != nullptr ? msg : "unknown error", rc);
}
}  // namespace

// ===========================================================================
// Database
// ===========================================================================
Database::Database(const std::string& path) {
    const int rc = sqlite3_open_v2(path.c_str(), &db_,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3* tmp = db_;
        db_ = nullptr;
        const char* msg = tmp != nullptr ? sqlite3_errmsg(tmp) : sqlite3_errstr(rc);
        DatabaseError err("open database", msg != nullptr ? msg : "unknown", rc);
        sqlite3_close(tmp);
        throw err;
    }
    sqlite3_busy_timeout(db_, 5000);
    exec("PRAGMA foreign_keys = ON;");
    exec("PRAGMA journal_mode = WAL;");
    exec("PRAGMA synchronous = NORMAL;");
}

Database::~Database() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
    }
}

Database::Database(Database&& other) noexcept : db_(std::exchange(other.db_, nullptr)) {}

Database& Database::operator=(Database&& other) noexcept {
    if (this != &other) {
        if (db_ != nullptr) {
            sqlite3_close(db_);
        }
        db_ = std::exchange(other.db_, nullptr);
    }
    return *this;
}

void Database::exec(std::string_view sql) {
    char* errmsg = nullptr;
    const std::string sql_copy(sql);
    const int rc = sqlite3_exec(db_, sql_copy.c_str(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string detail = errmsg != nullptr ? errmsg : sqlite3_errstr(rc);
        sqlite3_free(errmsg);
        throw DatabaseError("exec", detail, rc);
    }
}

Statement Database::prepare(std::string_view sql) {
    sqlite3_stmt* stmt = nullptr;
    const int rc =
        sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fail(db_, "prepare", rc);
    }
    return Statement(db_, stmt);
}

std::int64_t Database::last_insert_rowid() const {
    return sqlite3_last_insert_rowid(db_);
}
int Database::changes() const {
    return sqlite3_changes(db_);
}

std::int64_t Database::user_version() {
    Statement s = prepare("PRAGMA user_version;");
    if (!s.step()) {
        return 0;
    }
    return s.column_int64(0);
}

void Database::set_user_version(std::int64_t version) {
    exec("PRAGMA user_version = " + std::to_string(version) + ";");
}

// ===========================================================================
// Statement
// ===========================================================================
Statement::~Statement() {
    if (stmt_ != nullptr) {
        sqlite3_finalize(stmt_);
    }
}

Statement::Statement(Statement&& other) noexcept
    : db_(std::exchange(other.db_, nullptr)), stmt_(std::exchange(other.stmt_, nullptr)) {}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        if (stmt_ != nullptr) {
            sqlite3_finalize(stmt_);
        }
        db_ = std::exchange(other.db_, nullptr);
        stmt_ = std::exchange(other.stmt_, nullptr);
    }
    return *this;
}

void Statement::check(int rc, const char* what) const {
    if (rc != SQLITE_OK) {
        fail(db_, what, rc);
    }
}

Statement& Statement::bind(int index, std::int64_t value) {
    check(sqlite3_bind_int64(stmt_, index, value), "bind int64");
    return *this;
}

Statement& Statement::bind(int index, double value) {
    check(sqlite3_bind_double(stmt_, index, value), "bind double");
    return *this;
}

Statement& Statement::bind(int index, std::string_view value) {
    check(sqlite3_bind_text(stmt_, index, value.data(), static_cast<int>(value.size()),
                            SQLITE_TRANSIENT),
          "bind text");
    return *this;
}

Statement& Statement::bind_null(int index) {
    check(sqlite3_bind_null(stmt_, index), "bind null");
    return *this;
}

Statement& Statement::bind(int index, std::optional<std::int64_t> value) {
    return value ? bind(index, *value) : bind_null(index);
}

Statement& Statement::bind(int index, const std::optional<std::string>& value) {
    return value ? bind(index, std::string_view(*value)) : bind_null(index);
}

bool Statement::step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    fail(db_, "step", rc);
}

void Statement::run() {
    (void)step();
}

void Statement::reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

bool Statement::is_null(int column) const {
    return sqlite3_column_type(stmt_, column) == SQLITE_NULL;
}

std::int64_t Statement::column_int64(int column) const {
    return sqlite3_column_int64(stmt_, column);
}

double Statement::column_double(int column) const {
    return sqlite3_column_double(stmt_, column);
}

std::string Statement::column_text(int column) const {
    const auto* text = sqlite3_column_text(stmt_, column);
    if (text == nullptr) {
        return {};
    }
    const int bytes = sqlite3_column_bytes(stmt_, column);
    return std::string(reinterpret_cast<const char*>(text), static_cast<std::size_t>(bytes));
}

std::optional<std::int64_t> Statement::column_opt_int64(int column) const {
    if (is_null(column)) {
        return std::nullopt;
    }
    return column_int64(column);
}

std::optional<std::string> Statement::column_opt_text(int column) const {
    if (is_null(column)) {
        return std::nullopt;
    }
    return column_text(column);
}

// ===========================================================================
// Transaction
// ===========================================================================
Transaction::Transaction(Database& db) : db_(db) {
    db_.exec("BEGIN;");
}

Transaction::~Transaction() {
    if (active_) {
        try {
            db_.exec("ROLLBACK;");
        } catch (...) {
            // Destructors must not throw; a failed rollback on teardown is
            // logged by the caller path, not here.
        }
    }
}

void Transaction::commit() {
    if (active_) {
        db_.exec("COMMIT;");
        active_ = false;
    }
}

}  // namespace flowforge::storage
