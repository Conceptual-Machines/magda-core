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

int readUserVersion(sqlite3* db) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }
    int version = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        version = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return version;
}

void setUserVersion(sqlite3* db, int version) {
    // PRAGMA user_version does not accept bound parameters; the value is an
    // internal integer, not user data, so concatenation is safe here.
    const std::string sql = "PRAGMA user_version = " + std::to_string(version);
    execOrThrow(db, sql.c_str(), "set user_version");
}

bool tableExists(sqlite3* db, const char* table) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", -1,
                           &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, table, -1, SQLITE_TRANSIENT);
    const bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}

// PRAGMA foreign_key_check reports one row per dangling FK. Throw if any
// survived a table rebuild — that means we lost a referenced id.
void foreignKeyCheckOrThrow(sqlite3* db) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA foreign_key_check", -1, &stmt, nullptr) != SQLITE_OK) {
        throw MediaDatabaseError(std::string("foreign_key_check: ") + lastError(db));
    }
    const bool violation = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    if (violation) {
        throw MediaDatabaseError("table rebuild left dangling foreign keys");
    }
}

// The media_file column list, used for the explicit INSERT ... SELECT in the
// table rebuild. Listing columns (rather than SELECT *) keeps the copy
// correct regardless of column order between old and new tables.
constexpr const char* kMediaFileColumns =
    "id, path, kind, format, size_bytes, mtime_ns, content_hash, indexed_at, display_name, "
    "duration_s, sample_rate, channels, bpm, key_root, key_scale, rms, spectral_centroid, "
    "spectral_flatness, transient_density, key_confidence, "
    "bpm_user, key_root_user, key_scale_user, total_beats_user, beat_mode_user, warp_markers_json, "
    "shape, family, tonal, preset_kind";

// Target media_file schema after v9 (kind CHECK extended with 'progression').
// Mirrors media_file in Schema.hpp exactly, renamed for the rebuild.
constexpr const char* kMediaFileNewTableV9 = R"SQL(
CREATE TABLE media_file_new (
    id              INTEGER PRIMARY KEY,
    path            TEXT    NOT NULL UNIQUE,
    kind            TEXT    NOT NULL CHECK (kind IN ('audio','preset','clip','progression')),
    format          TEXT    NOT NULL,
    size_bytes      INTEGER NOT NULL,
    mtime_ns        INTEGER NOT NULL,
    content_hash    BLOB,
    indexed_at      INTEGER NOT NULL,
    display_name    TEXT,
    duration_s          REAL,
    sample_rate         INTEGER,
    channels            INTEGER,
    bpm                 REAL,
    key_root            TEXT,
    key_scale           TEXT,
    rms                 REAL,
    spectral_centroid   REAL,
    spectral_flatness   REAL,
    transient_density   REAL,
    key_confidence      REAL,
    bpm_user            REAL,
    key_root_user       TEXT,
    key_scale_user      TEXT,
    total_beats_user    REAL,
    beat_mode_user      INTEGER CHECK (beat_mode_user IN (0, 1)),
    warp_markers_json   TEXT,
    shape   TEXT CHECK (shape  IN ('one-shot','loop','sustained','unknown')),
    family  TEXT CHECK (family IN
                ('drum','bass','lead','pad','keys','guitar','orchestral',
                 'vocal','fx','texture','unknown')),
    tonal   INTEGER CHECK (tonal IN (0, 1)),
    preset_kind TEXT CHECK (preset_kind IN ('chain','rack','device'))
)
)SQL";

constexpr const char* kMediaFileIndexes = R"SQL(
CREATE INDEX IF NOT EXISTS idx_media_file_kind   ON media_file (kind);
CREATE INDEX IF NOT EXISTS idx_media_file_format ON media_file (format);
CREATE INDEX IF NOT EXISTS idx_media_file_bpm    ON media_file (bpm);
CREATE INDEX IF NOT EXISTS idx_media_file_key    ON media_file (key_root, key_scale);
CREATE INDEX IF NOT EXISTS idx_media_file_shape  ON media_file (shape);
CREATE INDEX IF NOT EXISTS idx_media_file_family ON media_file (family);
CREATE INDEX IF NOT EXISTS idx_media_file_indexed_at ON media_file (indexed_at);
)SQL";

// Tier 2 (structural change): rebuild media_file following SQLite's
// recommended recipe (sqlite.org/lang_altertable.html#otheralter). SQLite
// cannot ALTER a CHECK constraint, so extending the kind enum needs a full
// table rebuild. Ids are preserved so the media_embedding / media_tag /
// media_metadata FKs stay valid; all computed data (features, embeddings,
// tags) and user overrides survive — no re-scan.
void rebuildMediaFileTable(sqlite3* db) {
    // foreign_keys cannot be toggled inside a transaction; leave it off
    // across the swap so DROP TABLE doesn't cascade-delete the child rows.
    execOrThrow(db, "PRAGMA foreign_keys=OFF", "rebuild fk off");
    try {
        execOrThrow(db, "BEGIN", "rebuild begin");
        execOrThrow(db, kMediaFileNewTableV9, "rebuild create new");
        const std::string copy = std::string("INSERT INTO media_file_new (") + kMediaFileColumns +
                                 ") SELECT " + kMediaFileColumns + " FROM media_file";
        execOrThrow(db, copy.c_str(), "rebuild copy rows");
        execOrThrow(db, "DROP TABLE media_file", "rebuild drop old");
        execOrThrow(db, "ALTER TABLE media_file_new RENAME TO media_file", "rebuild rename");
        execOrThrow(db, kMediaFileIndexes, "rebuild indexes");
        foreignKeyCheckOrThrow(db);
        execOrThrow(db, "COMMIT", "rebuild commit");
    } catch (...) {
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA foreign_keys=ON", nullptr, nullptr, nullptr);
        throw;
    }
    execOrThrow(db, "PRAGMA foreign_keys=ON", "rebuild fk on");
}

// Version-gated migration ladder. Applies ordered steps for every version
// above `fromVersion`. The media DB is a rebuildable cache, so a step can
// never lose data, only recompute time. Tier 1 (additive ALTER ADD COLUMN)
// for new nullable columns; tier 2 (table rebuild) for structural/constraint
// changes SQLite cannot ALTER in place.
void migrate(sqlite3* db, int fromVersion) {
    // Tiers 1 (v4-v8): additive columns. Each ALTER is idempotent via
    // columnExists, so the block stays safe even against a DB whose
    // user_version was never tracked (reads as 0 -> runs everything).
    if (fromVersion < 8) {
        if (!columnExists(db, "media_file", "bpm_user")) {
            execOrThrow(db, "ALTER TABLE media_file ADD COLUMN bpm_user REAL",
                        "migrate v4 bpm_user");
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
        if (!columnExists(db, "media_file", "preset_kind")) {
            execOrThrow(db,
                        "ALTER TABLE media_file ADD COLUMN preset_kind TEXT "
                        "CHECK (preset_kind IN ('chain','rack','device'))",
                        "migrate v8 preset_kind");
        }
    }
    // Tier 2 (v9): kind='progression'. SQLite cannot ALTER the kind CHECK, so
    // rebuild the table. Runs after the additive block so the old table has
    // every column the copy expects.
    if (fromVersion < 9) {
        rebuildMediaFileTable(db);
    }
}

}  // namespace

void migrateMediaDatabase(sqlite3* db, int fromVersion) {
    migrate(db, fromVersion);
    setUserVersion(db, kSchemaVersion);
}

MediaDatabase::MediaDatabase(const std::filesystem::path& dbPath) : path_(dbPath) {
    int rc = sqlite3_open(dbPath.string().c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string err = lastError(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        throw MediaDatabaseError("sqlite3_open(" + dbPath.string() + "): " + err);
    }
    sqlite3_busy_timeout(db_, 60000);

    // Apply the schema, then bring the file up to the current version. The
    // version read and the fresh-DB probe happen before kSchemaSql so the
    // CREATE ... IF NOT EXISTS statements don't mask whether this was a brand
    // new file. A fresh DB gets the latest schema straight from kSchemaSql,
    // so it only needs its user_version stamped; an existing DB runs the
    // migration ladder (a DB predating version tracking reads as 0 and runs
    // every step, each of which is idempotent or a no-op when not needed).
    try {
        const int existingVersion = readUserVersion(db_);
        const bool freshDb = !tableExists(db_, "media_file");
        execOrThrow(db_, kSchemaSql, "schema init");
        if (freshDb) {
            setUserVersion(db_, kSchemaVersion);
        } else {
            migrateMediaDatabase(db_, existingVersion);
        }
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
