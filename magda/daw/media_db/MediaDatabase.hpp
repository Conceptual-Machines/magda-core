// MediaDatabase — RAII wrapper around a SQLite connection for the media
// catalogue (issue #768). Opens the DB at the given path, applying the
// embedded schema on first creation. Designed to be used from a single
// thread per instance (per SQLite multi-thread mode); the indexer runs on
// a background thread with its own MediaDatabase instance pointing at the
// same file.

#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>

struct sqlite3;

namespace magda::media {

// Bumped whenever Schema.hpp changes. The migration ladder (migrate() in
// MediaDatabase.cpp) reads PRAGMA user_version and applies ordered steps up
// to this value, then stamps it. The media DB is a rebuildable cache (files
// on disk are the source of truth), so a migration can never lose data,
// only recompute time.
//
// History: v4 user-override columns, v5 warp markers, v6 display_name,
// v7 total_beats/beat_mode, v8 preset_kind, v9 kind='progression' (extends
// the kind CHECK via a table rebuild).
inline constexpr int kSchemaVersion = 9;

class MediaDatabaseError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// Run the version-gated migration ladder against an open connection,
// upgrading from `fromVersion` to kSchemaVersion and stamping user_version.
// The MediaDatabase constructor calls this internally after applying the
// schema; it is exposed so tests can drive it against a hand-built old DB.
// Throws MediaDatabaseError on failure.
void migrateMediaDatabase(sqlite3* db, int fromVersion);

class MediaDatabase {
  public:
    // Opens (or creates) the SQLite file at dbPath. Throws MediaDatabaseError
    // on failure. ":memory:" gives an in-memory DB (used by tests).
    explicit MediaDatabase(const std::filesystem::path& dbPath);
    ~MediaDatabase();

    MediaDatabase(const MediaDatabase&) = delete;
    MediaDatabase& operator=(const MediaDatabase&) = delete;
    MediaDatabase(MediaDatabase&&) noexcept;
    MediaDatabase& operator=(MediaDatabase&&) noexcept;

    // Underlying SQLite handle for prepared statements in caller code.
    [[nodiscard]] sqlite3* handle() const noexcept {
        return db_;
    }

    // The path this connection was opened against (literally `:memory:` for
    // in-memory DBs). Lets the parallel indexer clone-open per-thread
    // connections to the same file under WAL.
    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

    // Run one or more semicolon-separated statements with no result set.
    // Throws MediaDatabaseError on failure.
    void execute(const std::string& sql);

    // PRAGMA user_version read-back, or -1 if the read fails.
    [[nodiscard]] int schemaVersion() const;

    // RAII transaction: BEGIN on construction, ROLLBACK on destruction
    // unless commit() was called. Use this around batched indexing inserts
    // so a partial scan doesn't leave the DB in a half-written state.
    class Transaction {
      public:
        explicit Transaction(MediaDatabase& db);
        ~Transaction();

        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;

        // Commit explicitly. After this, the destructor is a no-op.
        void commit();

      private:
        MediaDatabase* db_;
        bool finished_ = false;
    };

  private:
    sqlite3* db_ = nullptr;
    std::filesystem::path path_;
};

}  // namespace magda::media
