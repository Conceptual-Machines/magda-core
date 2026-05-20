#include "MediaDatabase.hpp"

#include <sqlite3.h>

#include <cstring>

#include "Schema.hpp"

namespace magda::media {

namespace {

[[nodiscard]] std::string lastError(sqlite3* db) {
    const char* msg = db ? sqlite3_errmsg(db) : "(no connection)";
    return msg ? msg : "(unknown error)";
}

void execOrThrow(sqlite3* db, const char* sql, const char* context) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::string err = errMsg ? errMsg : lastError(db);
        sqlite3_free(errMsg);
        throw MediaDatabaseError(std::string(context) + ": " + err);
    }
}

// True if `table` has a column named `column`. Used by the migration step
// to make ALTER TABLE ADD COLUMN idempotent — SQLite doesn't support
// `ADD COLUMN IF NOT EXISTS`.
bool columnExists(sqlite3* db, const char* table, const char* column) {
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string("PRAGMA table_info(") + table + ")";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (name && std::strcmp(name, column) == 0) {
            found = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

// Apply per-version migrations to an already-open DB. New columns introduced
// in schema bumps are added in place so users keep their indexed data
// instead of being forced to re-scan after an update.
void migrate(sqlite3* db) {
    // v3 → v4: user-override columns on media_file. NULL means "no
    // override" — read path uses COALESCE(_user, detected) so the UI sees
    // a single effective value.
    if (!columnExists(db, "media_file", "bpm_user")) {
        execOrThrow(db, "ALTER TABLE media_file ADD COLUMN bpm_user REAL", "migrate v4 bpm_user");
    }
    if (!columnExists(db, "media_file", "key_root_user")) {
        execOrThrow(db, "ALTER TABLE media_file ADD COLUMN key_root_user TEXT",
                    "migrate v4 key_root_user");
    }
    if (!columnExists(db, "media_file", "key_scale_user")) {
        execOrThrow(db, "ALTER TABLE media_file ADD COLUMN key_scale_user TEXT",
                    "migrate v4 key_scale_user");
    }
    if (!columnExists(db, "media_file", "warp_markers_json")) {
        execOrThrow(db, "ALTER TABLE media_file ADD COLUMN warp_markers_json TEXT",
                    "migrate v5 warp_markers_json");
    }
    if (!columnExists(db, "media_file", "display_name")) {
        execOrThrow(db, "ALTER TABLE media_file ADD COLUMN display_name TEXT",
                    "migrate v6 display_name");
    }
    if (!columnExists(db, "media_file", "total_beats_user")) {
        execOrThrow(db, "ALTER TABLE media_file ADD COLUMN total_beats_user REAL",
                    "migrate v7 total_beats_user");
    }
    if (!columnExists(db, "media_file", "beat_mode_user")) {
        execOrThrow(db, "ALTER TABLE media_file ADD COLUMN beat_mode_user INTEGER",
                    "migrate v7 beat_mode_user");
    }
}

}  // namespace

MediaDatabase::MediaDatabase(const std::filesystem::path& dbPath) : path_(dbPath) {
    int rc = sqlite3_open(dbPath.string().c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string err = lastError(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        throw MediaDatabaseError("sqlite3_open(" + dbPath.string() + "): " + err);
    }
    sqlite3_busy_timeout(db_, 60000);

    // Apply the schema. CREATE ... IF NOT EXISTS makes this safe to run on
    // every open, including against an already-initialized file. After
    // creation, run migrate() to bring older schemas up to date in place.
    try {
        execOrThrow(db_, kSchemaSql, "schema init");
        migrate(db_);
    } catch (...) {
        sqlite3_close(db_);
        db_ = nullptr;
        throw;
    }
}

MediaDatabase::~MediaDatabase() {
    if (db_) {
        sqlite3_close(db_);
    }
}

MediaDatabase::MediaDatabase(MediaDatabase&& other) noexcept
    : db_(other.db_), path_(std::move(other.path_)) {
    other.db_ = nullptr;
}

MediaDatabase& MediaDatabase::operator=(MediaDatabase&& other) noexcept {
    if (this != &other) {
        if (db_) {
            sqlite3_close(db_);
        }
        db_ = other.db_;
        path_ = std::move(other.path_);
        other.db_ = nullptr;
    }
    return *this;
}

void MediaDatabase::execute(const std::string& sql) {
    execOrThrow(db_, sql.c_str(), "execute");
}

int MediaDatabase::schemaVersion() const {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "PRAGMA user_version", -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }
    int version = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        version = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return version;
}

MediaDatabase::Transaction::Transaction(MediaDatabase& db) : db_(&db) {
    execOrThrow(db_->db_, "BEGIN", "BEGIN");
}

MediaDatabase::Transaction::~Transaction() {
    if (!finished_ && db_) {
        // Best-effort rollback on scope exit when commit() wasn't called
        // (typically because an exception is unwinding). Swallow errors —
        // throwing from a destructor is worse than the rollback failing.
        sqlite3_exec(db_->db_, "ROLLBACK", nullptr, nullptr, nullptr);
    }
}

void MediaDatabase::Transaction::commit() {
    if (finished_) {
        return;
    }
    execOrThrow(db_->db_, "COMMIT", "COMMIT");
    finished_ = true;
}

}  // namespace magda::media
