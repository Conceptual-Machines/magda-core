#include "MediaDbMetadata.hpp"

#include <sqlite3.h>

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

void setUserKeyForFile(const std::filesystem::path& path, std::optional<std::string> root,
                       std::optional<std::string> scale) {
    auto& ctx = MediaDbContext::getInstance();
    if (!ctx.ensureInitialized()) {
        return;
    }
    setUserKey(ctx.db(), path, std::move(root), std::move(scale));
}

}  // namespace magda::media
