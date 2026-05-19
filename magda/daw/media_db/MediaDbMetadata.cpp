#include "MediaDbMetadata.hpp"

#include <sqlite3.h>

#include <cctype>
#include <string>

#include "MediaDatabase.hpp"
#include "MediaDbContext.hpp"

namespace magda::media {

namespace {

std::optional<std::string> optString(sqlite3_stmt* stmt, int col) {
    if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
        return std::nullopt;
    }
    if (const auto* t = sqlite3_column_text(stmt, col)) {
        return std::string(reinterpret_cast<const char*>(t));
    }
    return std::nullopt;
}

std::optional<double> optDouble(sqlite3_stmt* stmt, int col) {
    if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
        return std::nullopt;
    }
    return sqlite3_column_double(stmt, col);
}

}  // namespace

std::optional<EffectiveMetadata> getEffectiveMetadata(MediaDatabase& db,
                                                      const std::filesystem::path& path) {
    static constexpr const char* kSql = "SELECT COALESCE(bpm_user, bpm), "
                                        "       COALESCE(key_root_user, key_root), "
                                        "       COALESCE(key_scale_user, key_scale) "
                                        "FROM media_file WHERE path = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db.handle(), kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    const std::string p = path.string();
    sqlite3_bind_text(stmt, 1, p.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<EffectiveMetadata> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        EffectiveMetadata m;
        m.bpm = optDouble(stmt, 0);
        m.keyRoot = optString(stmt, 1);
        m.keyScale = optString(stmt, 2);
        result = m;
    }
    sqlite3_finalize(stmt);
    return result;
}

void setUserBpm(MediaDatabase& db, const std::filesystem::path& path, std::optional<double> bpm) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db.handle(), "UPDATE media_file SET bpm_user = ? WHERE path = ?", -1,
                           &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    if (bpm) {
        sqlite3_bind_double(stmt, 1, *bpm);
    } else {
        sqlite3_bind_null(stmt, 1);
    }
    const std::string p = path.string();
    sqlite3_bind_text(stmt, 2, p.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void setUserKey(MediaDatabase& db, const std::filesystem::path& path,
                std::optional<std::string> root, std::optional<std::string> scale) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db.handle(),
                           "UPDATE media_file SET key_root_user = ?, key_scale_user = ? "
                           "WHERE path = ?",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    if (root) {
        sqlite3_bind_text(stmt, 1, root->c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 1);
    }
    if (scale) {
        sqlite3_bind_text(stmt, 2, scale->c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 2);
    }
    const std::string p = path.string();
    sqlite3_bind_text(stmt, 3, p.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void setUserKeyRoot(MediaDatabase& db, const std::filesystem::path& path,
                    std::optional<std::string> root) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db.handle(), "UPDATE media_file SET key_root_user = ? WHERE path = ?",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    if (root) {
        sqlite3_bind_text(stmt, 1, root->c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 1);
    }
    const std::string p = path.string();
    sqlite3_bind_text(stmt, 2, p.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// ---- Singleton convenience wrappers ------------------------------------

std::optional<EffectiveMetadata> getEffectiveMetadataForFile(const std::filesystem::path& path) {
    auto& ctx = MediaDbContext::getInstance();
    if (!ctx.ensureInitialized()) {
        return std::nullopt;
    }
    return getEffectiveMetadata(ctx.db(), path);
}

void setUserBpmForFile(const std::filesystem::path& path, std::optional<double> bpm) {
    auto& ctx = MediaDbContext::getInstance();
    if (!ctx.ensureInitialized()) {
        return;
    }
    setUserBpm(ctx.db(), path, bpm);
}

void setUserKeyRootForFile(const std::filesystem::path& path, std::optional<std::string> root) {
    auto& ctx = MediaDbContext::getInstance();
    if (!ctx.ensureInitialized()) {
        return;
    }
    setUserKeyRoot(ctx.db(), path, root);
}

void setUserKeyForFile(const std::filesystem::path& path, std::optional<std::string> root,
                       std::optional<std::string> scale) {
    auto& ctx = MediaDbContext::getInstance();
    if (!ctx.ensureInitialized()) {
        return;
    }
    setUserKey(ctx.db(), path, std::move(root), std::move(scale));
}

bool hasIndexedDescendant(MediaDatabase& db, const std::filesystem::path& folder) {
    // Prefix = folder path with a trailing separator so we match only
    // descendants (foo/bar/baz.wav), never the folder itself or sibling
    // entries (foo/barxyz.wav). Upper bound increments the last char so
    // the range query covers exactly the descendant set.
    std::string prefix = folder.string();
    if (prefix.empty()) {
        return false;
    }
    if (prefix.back() != '/' && prefix.back() != '\\') {
        prefix.push_back('/');
    }
    std::string upper = prefix;
    upper.back() = static_cast<char>(static_cast<unsigned char>(upper.back()) + 1);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db.handle(),
                           "SELECT 1 FROM media_file WHERE path >= ? AND path < ? LIMIT 1", -1,
                           &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, prefix.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, upper.c_str(), -1, SQLITE_TRANSIENT);
    const bool found = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return found;
}

bool hasIndexedDescendantOfFolder(const std::filesystem::path& folder) {
    auto& ctx = MediaDbContext::getInstance();
    if (!ctx.ensureInitialized()) {
        return false;
    }
    return hasIndexedDescendant(ctx.db(), folder);
}

int removeFolderFromLibrary(MediaDatabase& db, const std::filesystem::path& folder) {
    std::string prefix = folder.string();
    if (prefix.empty()) {
        return 0;
    }
    if (prefix.back() != '/' && prefix.back() != '\\') {
        prefix.push_back('/');
    }
    std::string upper = prefix;
    upper.back() = static_cast<char>(static_cast<unsigned char>(upper.back()) + 1);

    auto* handle = db.handle();
    sqlite3_exec(handle, "BEGIN", nullptr, nullptr, nullptr);

    // FTS first — once media_file rows are gone we'd have no rowids to
    // join against. The contentless FTS table holds path/tag text only,
    // no FK, so cascades don't reach it.
    {
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(handle,
                           "DELETE FROM media_fts WHERE rowid IN "
                           "(SELECT id FROM media_file WHERE path >= ? AND path < ?)",
                           -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, prefix.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, upper.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    int removed = 0;
    {
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(handle, "DELETE FROM media_file WHERE path >= ? AND path < ?", -1, &stmt,
                           nullptr);
        sqlite3_bind_text(stmt, 1, prefix.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, upper.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            removed = sqlite3_changes(handle);
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_exec(handle, "COMMIT", nullptr, nullptr, nullptr);
    return removed;
}

int removeFolderFromLibrary(const std::filesystem::path& folder) {
    auto& ctx = MediaDbContext::getInstance();
    if (!ctx.ensureInitialized()) {
        return 0;
    }
    return removeFolderFromLibrary(ctx.db(), folder);
}

namespace {

// Same normalisation the indexer applies on first insert (see
// MediaDbIndexer::buildPathText). Reimplemented here so we don't have to
// expose that helper across modules.
std::string pathTextFor(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        const bool isSep = c == '_' || c == '/' || c == '\\' || c == '-' || c == '.' || c == ',' ||
                           c == '(' || c == ')';
        out += isSep ? ' ' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

}  // namespace

int moveFolderInLibrary(MediaDatabase& db, const std::filesystem::path& oldFolder,
                        const std::filesystem::path& newFolder) {
    std::string oldPrefix = oldFolder.string();
    std::string newPrefix = newFolder.string();
    if (oldPrefix.empty() || newPrefix.empty()) {
        return 0;
    }
    if (oldPrefix.back() != '/' && oldPrefix.back() != '\\') {
        oldPrefix.push_back('/');
    }
    if (newPrefix.back() != '/' && newPrefix.back() != '\\') {
        newPrefix.push_back('/');
    }
    if (oldPrefix == newPrefix) {
        return 0;
    }
    std::string upper = oldPrefix;
    upper.back() = static_cast<char>(static_cast<unsigned char>(upper.back()) + 1);

    auto* handle = db.handle();
    if (sqlite3_exec(handle, "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK) {
        return -1;
    }

    int updatedRows = 0;
    bool ok = true;

    // Rewrite paths in one statement. substr(path, len(oldPrefix)+1) drops
    // the old prefix; concat with newPrefix gives the relocated path.
    {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(handle,
                               "UPDATE media_file SET path = ?1 || substr(path, ?2) "
                               "WHERE path >= ?3 AND path < ?4",
                               -1, &stmt, nullptr) != SQLITE_OK) {
            ok = false;
        } else {
            sqlite3_bind_text(stmt, 1, newPrefix.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, static_cast<int>(oldPrefix.size()) + 1);
            sqlite3_bind_text(stmt, 3, oldPrefix.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, upper.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_DONE) {
                updatedRows = sqlite3_changes(handle);
            } else {
                ok = false;  // UNIQUE collision or other error
            }
            sqlite3_finalize(stmt);
        }
    }

    // Refresh media_fts.path_text for the moved rows so a search by folder
    // name finds them at the new location.
    if (ok && updatedRows > 0) {
        std::string newUpper = newPrefix;
        newUpper.back() = static_cast<char>(static_cast<unsigned char>(newUpper.back()) + 1);

        sqlite3_stmt* selStmt = nullptr;
        sqlite3_prepare_v2(handle, "SELECT id, path FROM media_file WHERE path >= ? AND path < ?",
                           -1, &selStmt, nullptr);
        sqlite3_bind_text(selStmt, 1, newPrefix.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(selStmt, 2, newUpper.c_str(), -1, SQLITE_TRANSIENT);

        sqlite3_stmt* updStmt = nullptr;
        sqlite3_prepare_v2(handle, "UPDATE media_fts SET path_text = ? WHERE rowid = ?", -1,
                           &updStmt, nullptr);

        while (sqlite3_step(selStmt) == SQLITE_ROW) {
            const auto id = sqlite3_column_int64(selStmt, 0);
            const std::string rawPath =
                reinterpret_cast<const char*>(sqlite3_column_text(selStmt, 1));
            const auto pathText = pathTextFor(rawPath);
            sqlite3_bind_text(updStmt, 1, pathText.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(updStmt, 2, id);
            sqlite3_step(updStmt);
            sqlite3_reset(updStmt);
        }
        sqlite3_finalize(selStmt);
        sqlite3_finalize(updStmt);
    }

    if (!ok) {
        sqlite3_exec(handle, "ROLLBACK", nullptr, nullptr, nullptr);
        return -1;
    }
    sqlite3_exec(handle, "COMMIT", nullptr, nullptr, nullptr);
    return updatedRows;
}

int moveFolderInLibrary(const std::filesystem::path& oldFolder,
                        const std::filesystem::path& newFolder) {
    auto& ctx = MediaDbContext::getInstance();
    if (!ctx.ensureInitialized()) {
        return 0;
    }
    return moveFolderInLibrary(ctx.db(), oldFolder, newFolder);
}

}  // namespace magda::media
