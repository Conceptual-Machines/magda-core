// Tests for the Phase A SQLite skeleton (issue #768).
//
// Mirrors the Python prototype's tests/test_db.py — schema creation, blob
// layout, FK cascade, kind CHECK constraint — and adds an FTS5 smoke test
// because the C++ runtime depends on FTS being compiled into the bundled
// SQLite library.

#include <sqlite3.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "../magda/daw/media_db/MediaDatabase.hpp"
#include "../magda/daw/media_db/MediaDbMetadata.hpp"
#include "../magda/daw/media_db/Schema.hpp"

using Catch::Approx;
using magda::media::kSchemaVersion;
using magda::media::MediaDatabase;
using magda::media::MediaDatabaseError;
using magda::media::migrateMediaDatabase;
namespace fs = std::filesystem;

namespace {

// Helper: list all user table names in the open DB.
std::set<std::string> listTables(sqlite3* db) {
    std::set<std::string> names;
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type='table'", -1, &stmt,
                               nullptr) == SQLITE_OK);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        names.insert(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);
    return names;
}

class TempDir {
  public:
    TempDir() {
        path_ = fs::temp_directory_path() /
                ("magda_media_db_metadata_test_" + std::to_string(std::random_device{}()));
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    [[nodiscard]] const fs::path& path() const {
        return path_;
    }

  private:
    fs::path path_;
};

}  // namespace

TEST_CASE("MediaDatabase opens in-memory and applies schema", "[media_db][schema]") {
    MediaDatabase db(":memory:");
    auto tables = listTables(db.handle());

    REQUIRE(tables.count("media_file") == 1);
    REQUIRE(tables.count("media_embedding") == 1);
    REQUIRE(tables.count("media_tag") == 1);
    REQUIRE(tables.count("media_metadata") == 1);

    REQUIRE(db.schemaVersion() == kSchemaVersion);
}

TEST_CASE("FTS5 virtual table is queryable", "[media_db][fts]") {
    MediaDatabase db(":memory:");
    db.execute("INSERT INTO media_fts (rowid, path_text, tag_text) "
               "VALUES (1, 'kicks bass tech house punchy C', 'kick drum')");

    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db.handle(), "SELECT rowid FROM media_fts WHERE media_fts MATCH ?",
                               -1, &stmt, nullptr) == SQLITE_OK);
    sqlite3_bind_text(stmt, 1, "kick", -1, SQLITE_STATIC);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    REQUIRE(sqlite3_column_int(stmt, 0) == 1);
    sqlite3_finalize(stmt);
}

TEST_CASE("kind CHECK constraint rejects unknown values", "[media_db][schema]") {
    MediaDatabase db(":memory:");
    REQUIRE_THROWS_AS(db.execute("INSERT INTO media_file "
                                 "(path, kind, format, size_bytes, mtime_ns, indexed_at) "
                                 "VALUES ('x', 'bogus', 'wav', 0, 0, 0)"),
                      MediaDatabaseError);
}

TEST_CASE("media_embedding cascades on file delete", "[media_db][fk]") {
    MediaDatabase db(":memory:");
    db.execute("INSERT INTO media_file "
               "(path, kind, format, size_bytes, mtime_ns, indexed_at) "
               "VALUES ('x', 'audio', 'wav', 0, 0, 0)");

    // Bind a 4-element float32 vector as the embedding blob — this is the
    // exact byte layout the Python pack_vector function writes, so the
    // schema check is end-to-end with the prototype.
    const float vec[4] = {1.0F, -1.0F, 0.5F, 0.0F};
    sqlite3_stmt* ins = nullptr;
    REQUIRE(sqlite3_prepare_v2(db.handle(),
                               "INSERT INTO media_embedding VALUES "
                               "((SELECT id FROM media_file WHERE path='x'), 'm', 'v', 4, ?)",
                               -1, &ins, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_bind_blob(ins, 1, vec, sizeof(vec), SQLITE_STATIC) == SQLITE_OK);
    REQUIRE(sqlite3_step(ins) == SQLITE_DONE);
    sqlite3_finalize(ins);

    db.execute("DELETE FROM media_file WHERE path='x'");

    sqlite3_stmt* count = nullptr;
    REQUIRE(sqlite3_prepare_v2(db.handle(), "SELECT COUNT(*) FROM media_embedding", -1, &count,
                               nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(count) == SQLITE_ROW);
    REQUIRE(sqlite3_column_int(count, 0) == 0);
    sqlite3_finalize(count);
}

TEST_CASE("vector blob round-trips byte-identical", "[media_db][blob]") {
    MediaDatabase db(":memory:");
    db.execute("INSERT INTO media_file "
               "(path, kind, format, size_bytes, mtime_ns, indexed_at) "
               "VALUES ('x', 'audio', 'wav', 0, 0, 0)");

    constexpr int kDim = 8;
    std::vector<float> in(kDim);
    for (int i = 0; i < kDim; ++i) {
        in[i] = static_cast<float>(i) * 0.125F - 0.5F;
    }

    sqlite3_stmt* ins = nullptr;
    REQUIRE(sqlite3_prepare_v2(db.handle(),
                               "INSERT INTO media_embedding VALUES "
                               "((SELECT id FROM media_file WHERE path='x'), 'm', 'v', ?, ?)",
                               -1, &ins, nullptr) == SQLITE_OK);
    sqlite3_bind_int(ins, 1, kDim);
    sqlite3_bind_blob(ins, 2, in.data(), static_cast<int>(in.size() * sizeof(float)),
                      SQLITE_STATIC);
    REQUIRE(sqlite3_step(ins) == SQLITE_DONE);
    sqlite3_finalize(ins);

    sqlite3_stmt* sel = nullptr;
    REQUIRE(sqlite3_prepare_v2(db.handle(),
                               "SELECT vector_dim, vector_blob FROM media_embedding LIMIT 1", -1,
                               &sel, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(sel) == SQLITE_ROW);
    REQUIRE(sqlite3_column_int(sel, 0) == kDim);

    const auto* blob = static_cast<const std::byte*>(sqlite3_column_blob(sel, 1));
    int blobBytes = sqlite3_column_bytes(sel, 1);
    REQUIRE(blobBytes == kDim * static_cast<int>(sizeof(float)));

    std::vector<float> out(kDim);
    std::memcpy(out.data(), blob, static_cast<size_t>(blobBytes));
    sqlite3_finalize(sel);

    for (int i = 0; i < kDim; ++i) {
        REQUIRE(out[i] == in[i]);
    }
}

TEST_CASE("Transaction rolls back when commit() not called", "[media_db][txn]") {
    MediaDatabase db(":memory:");
    {
        MediaDatabase::Transaction txn(db);
        db.execute("INSERT INTO media_file "
                   "(path, kind, format, size_bytes, mtime_ns, indexed_at) "
                   "VALUES ('rolled-back', 'audio', 'wav', 0, 0, 0)");
        // commit() not called -> destructor rolls back
    }

    sqlite3_stmt* count = nullptr;
    REQUIRE(sqlite3_prepare_v2(db.handle(), "SELECT COUNT(*) FROM media_file", -1, &count,
                               nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(count) == SQLITE_ROW);
    REQUIRE(sqlite3_column_int(count, 0) == 0);
    sqlite3_finalize(count);
}

TEST_CASE("Transaction commits on commit()", "[media_db][txn]") {
    MediaDatabase db(":memory:");
    {
        MediaDatabase::Transaction txn(db);
        db.execute("INSERT INTO media_file "
                   "(path, kind, format, size_bytes, mtime_ns, indexed_at) "
                   "VALUES ('committed', 'audio', 'wav', 0, 0, 0)");
        txn.commit();
    }

    sqlite3_stmt* count = nullptr;
    REQUIRE(sqlite3_prepare_v2(db.handle(), "SELECT COUNT(*) FROM media_file", -1, &count,
                               nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(count) == SQLITE_ROW);
    REQUIRE(sqlite3_column_int(count, 0) == 1);
    sqlite3_finalize(count);
}

TEST_CASE("User metadata save marks indexed file and round-trips warp markers",
          "[media_db][metadata]") {
    MediaDatabase db(":memory:");
    db.execute("INSERT INTO media_file "
               "(path, kind, format, size_bytes, mtime_ns, indexed_at) "
               "VALUES ('sample.wav', 'audio', 'wav', 0, 0, 0)");

    REQUIRE(magda::media::isFileIndexed(db, "sample.wav"));

    std::vector<magda::media::WarpMarkerMetadata> markers{{0.0, 0.0}, {1.5, 2.0}};
    REQUIRE(magda::media::saveUserMetadata(db, "sample.wav", 128.0, std::string("D"), 16.0, true,
                                           markers));

    auto savedMarkers = magda::media::getUserWarpMarkers(db, "sample.wav");
    REQUIRE(savedMarkers);
    REQUIRE(savedMarkers->size() == 2);
    REQUIRE((*savedMarkers)[1].sourceSec == Approx(1.5));
    REQUIRE((*savedMarkers)[1].beat == Approx(2.0));

    auto effective = magda::media::getEffectiveMetadata(db, "sample.wav");
    REQUIRE(effective);
    REQUIRE(effective->bpm);
    REQUIRE(*effective->bpm == Approx(128.0));
    REQUIRE(effective->totalBeats);
    REQUIRE(*effective->totalBeats == Approx(16.0));
    REQUIRE(effective->beatMode);
    REQUIRE(*effective->beatMode);

    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db.handle(),
                               "SELECT bpm_user, key_root_user, total_beats_user, beat_mode_user, "
                               "warp_markers_json IS NOT NULL "
                               "FROM media_file WHERE path='sample.wav'",
                               -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    REQUIRE(sqlite3_column_double(stmt, 0) == Approx(128.0));
    REQUIRE(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))) == "D");
    REQUIRE(sqlite3_column_double(stmt, 2) == Approx(16.0));
    REQUIRE(sqlite3_column_int(stmt, 3) == 1);
    REQUIRE(sqlite3_column_int(stmt, 4) == 1);
    sqlite3_finalize(stmt);
}

TEST_CASE("User metadata read does not promote scanner values to saved clip props",
          "[media_db][metadata]") {
    MediaDatabase db(":memory:");
    db.execute("INSERT INTO media_file "
               "(path, kind, format, size_bytes, mtime_ns, indexed_at, bpm, key_root) "
               "VALUES ('detected.wav', 'audio', 'wav', 0, 0, 0, 172.0, 'A')");

    auto effective = magda::media::getEffectiveMetadata(db, "detected.wav");
    REQUIRE(effective);
    REQUIRE(effective->bpm);
    REQUIRE(*effective->bpm == Approx(172.0));
    REQUIRE(effective->keyRoot);
    REQUIRE(*effective->keyRoot == "A");

    auto saved = magda::media::getUserMetadata(db, "detected.wav");
    REQUIRE(saved);
    REQUIRE_FALSE(saved->bpm);
    REQUIRE_FALSE(saved->keyRoot);
    REQUIRE_FALSE(saved->totalBeats);
    REQUIRE_FALSE(saved->beatMode);
}

TEST_CASE("Editable media rows update display fields and delete cleanly", "[media_db][metadata]") {
    MediaDatabase db(":memory:");
    db.execute("INSERT INTO media_file "
               "(path, kind, format, size_bytes, mtime_ns, indexed_at, family, shape) "
               "VALUES ('sample.wav', 'audio', 'wav', 0, 0, 0, 'unknown', 'unknown')");

    auto row = magda::media::getEditableMediaRow(db, 1);
    REQUIRE(row);
    row->displayName = std::string("Renamed Break");
    row->family = "drum";
    row->shape = "loop";
    row->bpm = 172.0;
    row->keyRoot = std::string("F#");
    row->keyScale = std::string("maj");
    row->durationS = 6.5;
    row->tags = {"breakbeat", "amen"};
    REQUIRE(magda::media::updateEditableMediaRow(db, *row));

    auto edited = magda::media::getEditableMediaRow(db, 1);
    REQUIRE(edited);
    REQUIRE(edited->displayName == std::optional<std::string>("Renamed Break"));
    REQUIRE(edited->family == "drum");
    REQUIRE(edited->shape == "loop");
    REQUIRE(edited->bpm == Approx(172.0));
    REQUIRE(edited->keyRoot == std::optional<std::string>("F#"));
    REQUIRE(edited->keyScale == std::optional<std::string>("maj"));
    REQUIRE(edited->durationS == Approx(6.5));
    REQUIRE(edited->tags.size() == 2);

    sqlite3_stmt* fts = nullptr;
    REQUIRE(sqlite3_prepare_v2(db.handle(),
                               "SELECT rowid FROM media_fts WHERE media_fts MATCH 'renamed'", -1,
                               &fts, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(fts) == SQLITE_ROW);
    REQUIRE(sqlite3_column_int64(fts, 0) == 1);
    sqlite3_finalize(fts);

    REQUIRE(magda::media::deleteMediaRows(db, {1}) == 1);
    sqlite3_stmt* count = nullptr;
    REQUIRE(sqlite3_prepare_v2(db.handle(), "SELECT COUNT(*) FROM media_file", -1, &count,
                               nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(count) == SQLITE_ROW);
    REQUIRE(sqlite3_column_int(count, 0) == 0);
    sqlite3_finalize(count);

    REQUIRE(sqlite3_prepare_v2(db.handle(), "SELECT COUNT(*) FROM media_fts", -1, &count,
                               nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(count) == SQLITE_ROW);
    REQUIRE(sqlite3_column_int(count, 0) == 0);
    sqlite3_finalize(count);
}

TEST_CASE("Duplicate file path cleanup removes rows pointing to the same physical file",
          "[media_db][metadata]") {
    TempDir dir;
    const auto original = dir.path() / "break.wav";
    const auto alias = dir.path() / "break-alias.wav";
    {
        std::ofstream out(original);
        out << "same audio bytes";
    }
    std::error_code ec;
    fs::create_hard_link(original, alias, ec);
    if (ec) {
        SUCCEED("filesystem does not allow hard links in this test location");
        return;
    }

    MediaDatabase db(":memory:");
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db.handle(),
                               "INSERT INTO media_file "
                               "(path, kind, format, size_bytes, mtime_ns, indexed_at, bpm_user) "
                               "VALUES (?, 'audio', 'wav', 16, 1, ?, ?)",
                               -1, &stmt, nullptr) == SQLITE_OK);
    const auto originalText = original.string();
    const auto aliasText = alias.string();
    sqlite3_bind_text(stmt, 1, originalText.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, 10);
    sqlite3_bind_double(stmt, 3, 172.0);
    REQUIRE(sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);

    sqlite3_bind_text(stmt, 1, aliasText.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, 20);
    sqlite3_bind_null(stmt, 3);
    REQUIRE(sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);

    REQUIRE(magda::media::removeDuplicateFilePathRows(db) == 1);

    REQUIRE(sqlite3_prepare_v2(db.handle(), "SELECT path, bpm_user FROM media_file", -1, &stmt,
                               nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    REQUIRE(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))) ==
            originalText);
    REQUIRE(sqlite3_column_double(stmt, 1) == Approx(172.0));
    REQUIRE(sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
}

TEST_CASE("Migration ladder rebuilds media_file to accept kind='progression'",
          "[media_db][migration]") {
    // Build a pre-v9 DB by hand: the kind CHECK lacks 'progression', plus the
    // full v8 column set and a child table to prove FK preservation across the
    // table rebuild. MediaDatabase always applies the latest schema, so the
    // old shape has to be created against a raw handle.
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db, "PRAGMA foreign_keys=ON", nullptr, nullptr, nullptr) == SQLITE_OK);

    const char* oldSchema = R"SQL(
        CREATE TABLE media_file (
            id INTEGER PRIMARY KEY,
            path TEXT NOT NULL UNIQUE,
            kind TEXT NOT NULL CHECK (kind IN ('audio','preset','clip')),
            format TEXT NOT NULL,
            size_bytes INTEGER NOT NULL,
            mtime_ns INTEGER NOT NULL,
            content_hash BLOB,
            indexed_at INTEGER NOT NULL,
            display_name TEXT,
            duration_s REAL, sample_rate INTEGER, channels INTEGER, bpm REAL,
            key_root TEXT, key_scale TEXT, rms REAL, spectral_centroid REAL,
            spectral_flatness REAL, transient_density REAL, key_confidence REAL,
            bpm_user REAL, key_root_user TEXT, key_scale_user TEXT,
            total_beats_user REAL, beat_mode_user INTEGER CHECK (beat_mode_user IN (0,1)),
            warp_markers_json TEXT,
            shape TEXT, family TEXT, tonal INTEGER,
            preset_kind TEXT CHECK (preset_kind IN ('chain','rack','device'))
        );
        CREATE TABLE media_tag (
            file_id INTEGER NOT NULL REFERENCES media_file(id) ON DELETE CASCADE,
            tag TEXT NOT NULL, confidence REAL NOT NULL, source_model TEXT NOT NULL,
            PRIMARY KEY (file_id, tag, source_model)
        );
        PRAGMA user_version = 8;
    )SQL";
    REQUIRE(sqlite3_exec(db, oldSchema, nullptr, nullptr, nullptr) == SQLITE_OK);

    REQUIRE(sqlite3_exec(
                db,
                "INSERT INTO media_file (id, path, kind, format, size_bytes, mtime_ns, indexed_at, "
                "bpm_user, key_root_user) VALUES (42, 'loop.wav', 'audio', 'wav', 10, 1, 2, 174.0, "
                "'G')",
                nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db, "INSERT INTO media_tag VALUES (42, 'amen', 1.0, 'path')", nullptr,
                         nullptr, nullptr) == SQLITE_OK);

    // The old CHECK rejects 'progression'.
    REQUIRE(sqlite3_exec(db,
                         "INSERT INTO media_file (path, kind, format, size_bytes, mtime_ns, "
                         "indexed_at) VALUES ('p.mid', 'progression', 'mid', 0, 0, 0)",
                         nullptr, nullptr, nullptr) != SQLITE_OK);

    migrateMediaDatabase(db, 8);

    // The rebuilt CHECK accepts 'progression'.
    REQUIRE(sqlite3_exec(db,
                         "INSERT INTO media_file (path, kind, format, size_bytes, mtime_ns, "
                         "indexed_at) VALUES ('p.mid', 'progression', 'mid', 0, 0, 0)",
                         nullptr, nullptr, nullptr) == SQLITE_OK);

    // Row id and user overrides survive the rebuild.
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "SELECT bpm_user, key_root_user FROM media_file WHERE id=42", -1,
                               &stmt, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    REQUIRE(sqlite3_column_double(stmt, 0) == Approx(174.0));
    REQUIRE(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))) == "G");
    sqlite3_finalize(stmt);

    // user_version is stamped to the current schema version.
    sqlite3_stmt* ver = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &ver, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(ver) == SQLITE_ROW);
    REQUIRE(sqlite3_column_int(ver, 0) == kSchemaVersion);
    sqlite3_finalize(ver);

    // The FK is preserved and still cascades: deleting the file clears its tag.
    REQUIRE(sqlite3_exec(db, "DELETE FROM media_file WHERE id=42", nullptr, nullptr, nullptr) ==
            SQLITE_OK);
    sqlite3_stmt* tag = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM media_tag", -1, &tag, nullptr) ==
            SQLITE_OK);
    REQUIRE(sqlite3_step(tag) == SQLITE_ROW);
    REQUIRE(sqlite3_column_int(tag, 0) == 0);
    sqlite3_finalize(tag);

    sqlite3_close(db);
}

TEST_CASE("Fresh DB reports current schema version and accepts progression rows",
          "[media_db][migration]") {
    MediaDatabase db(":memory:");
    REQUIRE(db.schemaVersion() == kSchemaVersion);
    REQUIRE_NOTHROW(db.execute("INSERT INTO media_file "
                               "(path, kind, format, size_bytes, mtime_ns, indexed_at) "
                               "VALUES ('p.mid', 'progression', 'mid', 0, 0, 0)"));
}
