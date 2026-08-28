#pragma once

#include <cstdint>

namespace flowforge::storage {

class Database;

// The schema version this build expects. Bump together with a new migration
// step in schema.cpp.
inline constexpr std::int64_t kCurrentSchemaVersion = 1;

// ---------------------------------------------------------------------------
// Bring @p db up to kCurrentSchemaVersion by applying the numbered migrations
// after its current PRAGMA user_version, inside a single transaction. A
// database that is already current is untouched. A database from a *newer*
// build (user_version > kCurrentSchemaVersion) is rejected with DatabaseError
// rather than silently misused.
// ---------------------------------------------------------------------------
void migrate_to_latest(Database& db);

}  // namespace flowforge::storage
