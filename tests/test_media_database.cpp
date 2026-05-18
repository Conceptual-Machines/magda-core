// Tests for the Phase A SQLite skeleton (issue #768).
//
// Mirrors the Python prototype's tests/test_db.py — schema creation, blob
// layout, FK cascade, kind CHECK constraint — and adds an FTS5 smoke test
// because the C++ runtime depends on FTS being compiled into the bundled
// SQLite library.

#include <sqlite3.h>

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "../magda/daw/media_db/MediaDatabase.hpp"
#include "../magda/daw/media_db/Schema.hpp"

using magda::media::kSchemaVersion;
using magda::media::MediaDatabase;
using magda::media::MediaDatabaseError;

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
