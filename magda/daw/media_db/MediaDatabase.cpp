#include "MediaDatabase.hpp"

#include <sqlite3.h>

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

}  // namespace

MediaDatabase::MediaDatabase(const std::filesystem::path& dbPath) : path_(dbPath) {
    int rc = sqlite3_open(dbPath.string().c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string err = lastError(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        throw MediaDatabaseError("sqlite3_open(" + dbPath.string() + "): " + err);
    }

    // Apply the schema. CREATE ... IF NOT EXISTS makes this safe to run on
    // every open, including against an already-initialized file.
    try {
        execOrThrow(db_, kSchemaSql, "schema init");
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
