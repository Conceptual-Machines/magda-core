#include "PresetDbIndexer.hpp"

#include <sqlite3.h>

#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <system_error>

#include "MediaDatabase.hpp"

namespace magda::media {

namespace {

constexpr const char* kPresetExtension = ".mps";

// Derive preset_kind from the first folder segment under presetsRoot.
// Returns empty string when the path doesn't sit under a recognized
// preset subtree, which the caller treats as "skip".
std::string presetKindFromPath(const std::filesystem::path& presetsRoot,
                               const std::filesystem::path& file) {
    std::error_code ec;
    const auto rel = std::filesystem::relative(file, presetsRoot, ec);
    if (ec || rel.empty()) {
        return {};
    }
    const auto first = rel.begin();
    if (first == rel.end()) {
        return {};
    }
    const auto seg = first->string();
    if (seg == "Chains") {
        return "chain";
    }
    if (seg == "Racks") {
        return "rack";
    }
    if (seg == "Devices") {
        return "device";
    }
    if (seg == "Curves") {
        return "curve";
    }
    return {};
}

struct FileMeta {
    std::int64_t sizeBytes = 0;
    std::int64_t mtimeNs = 0;
};

std::optional<FileMeta> statFile(const std::filesystem::path& path) {
    std::error_code ec;
    const auto sz = std::filesystem::file_size(path, ec);
    if (ec) {
        return std::nullopt;
    }
    const auto fileTime = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return std::nullopt;
    }
    // Only relative changes matter for skip-if-unchanged checks, so use the
    // file clock's own epoch directly. This avoids the non-portable
    // file_clock -> system_clock conversion (MSVC's file clock has no to_sys).
    FileMeta m;
    m.sizeBytes = static_cast<std::int64_t>(sz);
    m.mtimeNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(fileTime.time_since_epoch()).count();
    return m;
}

struct ExistingRow {
    std::int64_t id = -1;
    std::int64_t mtimeNs = 0;
    std::int64_t sizeBytes = 0;
};

std::optional<ExistingRow> lookupExisting(sqlite3* db, const std::string& path) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT id, mtime_ns, size_bytes FROM media_file WHERE path = ?", -1,
                           &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<ExistingRow> out;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        ExistingRow r;
        r.id = sqlite3_column_int64(stmt, 0);
        r.mtimeNs = sqlite3_column_int64(stmt, 1);
        r.sizeBytes = sqlite3_column_int64(stmt, 2);
        out = r;
    }
    sqlite3_finalize(stmt);
    return out;
}

// Same tokenisation MediaDbIndexer uses for path_text — lowercase, replace
// common separators with spaces. Gives FTS5's unicode61 tokenizer clean
// tokens to index.
std::string buildPathText(const std::filesystem::path& path) {
    const std::string raw = path.string();
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        const bool isSep = c == '_' || c == '/' || c == '\\' || c == '-' || c == '.' || c == ',' ||
                           c == '(' || c == ')';
        out += isSep ? ' ' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

void upsertFts(sqlite3* db, std::int64_t fileId, const std::string& pathText) {
    sqlite3_stmt* del = nullptr;
    sqlite3_prepare_v2(db, "DELETE FROM media_fts WHERE rowid = ?", -1, &del, nullptr);
    sqlite3_bind_int64(del, 1, fileId);
    sqlite3_step(del);
    sqlite3_finalize(del);

    sqlite3_stmt* ins = nullptr;
    sqlite3_prepare_v2(db, "INSERT INTO media_fts (rowid, path_text, tag_text) VALUES (?, ?, ?)",
                       -1, &ins, nullptr);
    sqlite3_bind_int64(ins, 1, fileId);
    sqlite3_bind_text(ins, 2, pathText.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 3, "", -1, SQLITE_STATIC);
    sqlite3_step(ins);
    sqlite3_finalize(ins);
}

// Insert or update a preset row. Returns the row id, or -1 on failure.
std::int64_t upsertPresetRow(sqlite3* db, const std::filesystem::path& path,
                             const std::string& presetKind, const FileMeta& meta) {
    static constexpr const char* kSql = R"SQL(
        INSERT INTO media_file (
            path, kind, format, size_bytes, mtime_ns, indexed_at,
            display_name, preset_kind
        ) VALUES (
            :path, 'preset', 'mps', :size, :mtime, :indexed,
            :display_name, :preset_kind
        )
        ON CONFLICT(path) DO UPDATE SET
            kind = excluded.kind,
            format = excluded.format,
            size_bytes = excluded.size_bytes,
            mtime_ns = excluded.mtime_ns,
            indexed_at = excluded.indexed_at,
            display_name = excluded.display_name,
            preset_kind = excluded.preset_kind
        RETURNING id
    )SQL";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }
    const std::string pathStr = path.string();
    const std::string displayName = path.stem().string();
    const auto now = static_cast<std::int64_t>(std::time(nullptr));

    auto p = [&](const char* name) { return sqlite3_bind_parameter_index(stmt, name); };

    sqlite3_bind_text(stmt, p(":path"), pathStr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, p(":size"), meta.sizeBytes);
    sqlite3_bind_int64(stmt, p(":mtime"), meta.mtimeNs);
    sqlite3_bind_int64(stmt, p(":indexed"), now);
    sqlite3_bind_text(stmt, p(":display_name"), displayName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, p(":preset_kind"), presetKind.c_str(), -1, SQLITE_TRANSIENT);

    std::int64_t id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return id;
}

}  // namespace

PresetDbIndexer::PresetDbIndexer(MediaDatabase& db) : db_(db) {}

PresetDbIndexer::Stats PresetDbIndexer::indexAll(const std::filesystem::path& presetsRoot) {
    Stats stats;
    std::error_code ec;
    if (!std::filesystem::is_directory(presetsRoot, ec)) {
        return stats;
    }

    // One transaction around the whole scan so a partial run doesn't
    // leave the DB in a half-written state, matching MediaDbIndexer.
    MediaDatabase::Transaction tx(db_);
    sqlite3* handle = db_.handle();

    for (const auto& subdir : {"Chains", "Racks", "Devices"}) {
        const auto root = presetsRoot / subdir;
        if (!std::filesystem::is_directory(root, ec)) {
            continue;
        }
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 root, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            if (!entry.is_regular_file(ec)) {
                continue;
            }
            if (entry.path().extension() != kPresetExtension) {
                continue;
            }

            const auto meta = statFile(entry.path());
            if (!meta) {
                ++stats.failed;
                continue;
            }
            const auto presetKind = presetKindFromPath(presetsRoot, entry.path());
            if (presetKind.empty()) {
                continue;  // outside the recognized subtree
            }

            const std::string pathStr = entry.path().string();
            const auto existing = lookupExisting(handle, pathStr);
            const bool unchanged = existing && existing->mtimeNs == meta->mtimeNs &&
                                   existing->sizeBytes == meta->sizeBytes;
            if (unchanged) {
                ++stats.skipped;
                continue;
            }

            const auto id = upsertPresetRow(handle, entry.path(), presetKind, *meta);
            if (id < 0) {
                ++stats.failed;
                continue;
            }
            upsertFts(handle, id, buildPathText(entry.path()));
            if (existing) {
                ++stats.updated;
            } else {
                ++stats.inserted;
            }
        }
    }

    tx.commit();
    return stats;
}

bool PresetDbIndexer::upsertOne(const std::filesystem::path& presetsRoot,
                                const std::filesystem::path& path) {
    const auto meta = statFile(path);
    if (!meta) {
        return false;
    }
    const auto presetKind = presetKindFromPath(presetsRoot, path);
    if (presetKind.empty()) {
        return false;
    }

    MediaDatabase::Transaction tx(db_);
    sqlite3* handle = db_.handle();
    const auto id = upsertPresetRow(handle, path, presetKind, *meta);
    if (id < 0) {
        return false;
    }
    upsertFts(handle, id, buildPathText(path));
    tx.commit();
    return true;
}

bool PresetDbIndexer::removeOne(const std::filesystem::path& path) {
    sqlite3* handle = db_.handle();
    const std::string pathStr = path.string();

    // Look up id so we can drop the FTS row too — the FTS5 table is
    // contentless and doesn't follow FKs, so the media_file CASCADE
    // doesn't help here.
    sqlite3_stmt* sel = nullptr;
    if (sqlite3_prepare_v2(handle, "SELECT id FROM media_file WHERE path = ?", -1, &sel, nullptr) !=
        SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(sel, 1, pathStr.c_str(), -1, SQLITE_TRANSIENT);
    std::int64_t id = -1;
    if (sqlite3_step(sel) == SQLITE_ROW) {
        id = sqlite3_column_int64(sel, 0);
    }
    sqlite3_finalize(sel);
    if (id < 0) {
        return true;  // already absent
    }

    MediaDatabase::Transaction tx(db_);
    sqlite3_stmt* delFts = nullptr;
    sqlite3_prepare_v2(handle, "DELETE FROM media_fts WHERE rowid = ?", -1, &delFts, nullptr);
    sqlite3_bind_int64(delFts, 1, id);
    sqlite3_step(delFts);
    sqlite3_finalize(delFts);

    sqlite3_stmt* delRow = nullptr;
    sqlite3_prepare_v2(handle, "DELETE FROM media_file WHERE id = ?", -1, &delRow, nullptr);
    sqlite3_bind_int64(delRow, 1, id);
    const int rc = sqlite3_step(delRow);
    sqlite3_finalize(delRow);
    if (rc != SQLITE_DONE) {
        return false;
    }
    tx.commit();
    return true;
}

}  // namespace magda::media
