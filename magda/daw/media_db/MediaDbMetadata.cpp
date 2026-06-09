#include "MediaDbMetadata.hpp"

#include <juce_core/juce_core.h>
#include <sqlite3.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <unordered_map>

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

std::string encodeWarpMarkers(const std::vector<WarpMarkerMetadata>& markers) {
    juce::Array<juce::var> arr;
    arr.ensureStorageAllocated(static_cast<int>(markers.size()));
    for (const auto& marker : markers) {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("source_sec", marker.sourceSec);
        obj->setProperty("beat", marker.beat);
        arr.add(juce::var(obj));
    }
    return juce::JSON::toString(juce::var(arr), true).toStdString();
}

std::optional<std::vector<WarpMarkerMetadata>> decodeWarpMarkers(const std::string& json) {
    auto parsed = juce::JSON::parse(juce::String(json));
    auto* arr = parsed.getArray();
    if (arr == nullptr) {
        return std::nullopt;
    }

    std::vector<WarpMarkerMetadata> markers;
    markers.reserve(static_cast<size_t>(arr->size()));
    for (const auto& item : *arr) {
        auto* obj = item.getDynamicObject();
        if (obj == nullptr) {
            return std::nullopt;
        }
        markers.push_back({static_cast<double>(obj->getProperty("source_sec")),
                           static_cast<double>(obj->getProperty("beat"))});
    }
    return markers;
}

struct FilePathDedupeRow {
    std::int64_t fileId = -1;
    std::filesystem::path path;
    std::int64_t indexedAt = 0;
    bool userEdited = false;
    bool analyzed = false;
};

bool isRegularFileMode(const int mode) {
#if defined(_WIN32)
    return (mode & _S_IFMT) == _S_IFREG;
#else
    return S_ISREG(mode);
#endif
}

std::optional<std::string> physicalFileKey(const std::filesystem::path& path) {
    struct stat st {};
    const auto text = path.string();
    if (::stat(text.c_str(), &st) != 0 || !isRegularFileMode(st.st_mode)) {
        return std::nullopt;
    }
    return std::to_string(static_cast<unsigned long long>(st.st_dev)) + ":" +
           std::to_string(static_cast<unsigned long long>(st.st_ino));
}

bool isBetterDuplicateKeeper(const FilePathDedupeRow& candidate, const FilePathDedupeRow& current) {
    if (candidate.userEdited != current.userEdited) {
        return candidate.userEdited;
    }
    if (candidate.analyzed != current.analyzed) {
        return candidate.analyzed;
    }
    if (candidate.indexedAt != current.indexedAt) {
        return candidate.indexedAt > current.indexedAt;
    }
    return candidate.fileId < current.fileId;
}

}  // namespace

std::optional<EffectiveMetadata> getEffectiveMetadata(MediaDatabase& db,
                                                      const std::filesystem::path& path) {
    static constexpr const char* kSql = "SELECT COALESCE(bpm_user, bpm), "
                                        "       COALESCE(key_root_user, key_root), "
                                        "       COALESCE(key_scale_user, key_scale), "
                                        "       total_beats_user, beat_mode_user "
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
        m.totalBeats = optDouble(stmt, 3);
        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
            m.beatMode = sqlite3_column_int(stmt, 4) != 0;
        }
        result = m;
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<EffectiveMetadata> getUserMetadata(MediaDatabase& db,
                                                 const std::filesystem::path& path) {
    static constexpr const char* kSql = "SELECT bpm_user, key_root_user, key_scale_user, "
                                        "       total_beats_user, beat_mode_user "
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
        m.totalBeats = optDouble(stmt, 3);
        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
            m.beatMode = sqlite3_column_int(stmt, 4) != 0;
        }
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

bool isFileIndexed(MediaDatabase& db, const std::filesystem::path& path) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db.handle(), "SELECT 1 FROM media_file WHERE path = ? LIMIT 1", -1,
                           &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    const std::string p = path.string();
    sqlite3_bind_text(stmt, 1, p.c_str(), -1, SQLITE_TRANSIENT);
    const bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}

std::optional<EditableMediaRow> getEditableMediaRow(MediaDatabase& db, std::int64_t fileId) {
    static constexpr const char* kSql =
        "SELECT id, path, display_name, family, shape, COALESCE(bpm_user, bpm), "
        "COALESCE(key_root_user, key_root), COALESCE(key_scale_user, key_scale), duration_s, "
        "(SELECT GROUP_CONCAT(tag, ', ') FROM media_tag WHERE file_id = media_file.id) "
        "FROM media_file WHERE id = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db.handle(), kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    sqlite3_bind_int64(stmt, 1, fileId);

    std::optional<EditableMediaRow> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        EditableMediaRow row;
        row.fileId = sqlite3_column_int64(stmt, 0);
        if (const auto* path = sqlite3_column_text(stmt, 1)) {
            row.path = std::filesystem::path(reinterpret_cast<const char*>(path));
        }
        row.displayName = optString(stmt, 2);
        if (auto family = optString(stmt, 3)) {
            row.family = *family;
        }
        if (auto shape = optString(stmt, 4)) {
            row.shape = *shape;
        }
        row.bpm = optDouble(stmt, 5);
        row.keyRoot = optString(stmt, 6);
        row.keyScale = optString(stmt, 7);
        row.durationS = optDouble(stmt, 8);
        if (auto tagsCsv = optString(stmt, 9)) {
            std::stringstream stream(*tagsCsv);
            std::string tag;
            while (std::getline(stream, tag, ',')) {
                while (!tag.empty() && std::isspace(static_cast<unsigned char>(tag.front()))) {
                    tag.erase(tag.begin());
                }
                while (!tag.empty() && std::isspace(static_cast<unsigned char>(tag.back()))) {
                    tag.pop_back();
                }
                if (!tag.empty()) {
                    row.tags.push_back(tag);
                }
            }
        }
        result = std::move(row);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<std::vector<WarpMarkerMetadata>> getUserWarpMarkers(
    MediaDatabase& db, const std::filesystem::path& path) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db.handle(), "SELECT warp_markers_json FROM media_file WHERE path = ?",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    const std::string p = path.string();
    sqlite3_bind_text(stmt, 1, p.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<std::vector<WarpMarkerMetadata>> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        if (auto json = optString(stmt, 0)) {
            result = decodeWarpMarkers(*json);
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

void setUserWarpMarkers(MediaDatabase& db, const std::filesystem::path& path,
                        std::optional<std::vector<WarpMarkerMetadata>> markers) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db.handle(),
                           "UPDATE media_file SET warp_markers_json = ? WHERE path = ?", -1, &stmt,
                           nullptr) != SQLITE_OK) {
        return;
    }
    std::string json;
    if (markers) {
        json = encodeWarpMarkers(*markers);
        sqlite3_bind_text(stmt, 1, json.c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 1);
    }
    const std::string p = path.string();
    sqlite3_bind_text(stmt, 2, p.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

bool saveUserMetadata(MediaDatabase& db, const std::filesystem::path& path,
                      std::optional<double> bpm, std::optional<std::string> keyRoot,
                      std::optional<double> totalBeats, std::optional<bool> beatMode,
                      std::optional<std::vector<WarpMarkerMetadata>> warpMarkers) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db.handle(),
                           "UPDATE media_file SET bpm_user = ?, key_root_user = ?, "
                           "total_beats_user = ?, beat_mode_user = ?, "
                           "warp_markers_json = ? WHERE path = ?",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    if (bpm) {
        sqlite3_bind_double(stmt, 1, *bpm);
    } else {
        sqlite3_bind_null(stmt, 1);
    }
    if (keyRoot && !keyRoot->empty()) {
        sqlite3_bind_text(stmt, 2, keyRoot->c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 2);
    }
    if (totalBeats) {
        sqlite3_bind_double(stmt, 3, *totalBeats);
    } else {
        sqlite3_bind_null(stmt, 3);
    }
    if (beatMode) {
        sqlite3_bind_int(stmt, 4, *beatMode ? 1 : 0);
    } else {
        sqlite3_bind_null(stmt, 4);
    }

    std::string json;
    if (warpMarkers) {
        json = encodeWarpMarkers(*warpMarkers);
        sqlite3_bind_text(stmt, 5, json.c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 5);
    }

    const std::string p = path.string();
    sqlite3_bind_text(stmt, 6, p.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE &&
                    (sqlite3_changes(db.handle()) > 0 || isFileIndexed(db, path));
    sqlite3_finalize(stmt);
    return ok;
}

// ---- Singleton convenience wrappers ------------------------------------

std::optional<EffectiveMetadata> getEffectiveMetadataForFile(const std::filesystem::path& path) {
    auto& ctx = MediaDbContext::getInstance();
    if (!ctx.ensureInitialized()) {
        return std::nullopt;
    }
    return getEffectiveMetadata(ctx.db(), path);
}

std::optional<EffectiveMetadata> getUserMetadataForFile(const std::filesystem::path& path) {
    auto& ctx = MediaDbContext::getInstance();
    if (!ctx.ensureInitialized()) {
        return std::nullopt;
    }
    return getUserMetadata(ctx.db(), path);
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

bool isFileIndexed(const std::filesystem::path& path) {
    auto& ctx = MediaDbContext::getInstance();
    if (!ctx.ensureInitialized()) {
        return false;
    }
    return isFileIndexed(ctx.db(), path);
}

std::optional<std::vector<WarpMarkerMetadata>> getUserWarpMarkersForFile(
    const std::filesystem::path& path) {
    auto& ctx = MediaDbContext::getInstance();
    if (!ctx.ensureInitialized()) {
        return std::nullopt;
    }
    return getUserWarpMarkers(ctx.db(), path);
}

bool saveUserMetadataForFile(const std::filesystem::path& path, std::optional<double> bpm,
                             std::optional<std::string> keyRoot, std::optional<double> totalBeats,
                             std::optional<bool> beatMode,
                             std::optional<std::vector<WarpMarkerMetadata>> warpMarkers) {
    auto& ctx = MediaDbContext::getInstance();
    if (!ctx.ensureInitialized()) {
        return false;
    }
    const bool ok = saveUserMetadata(ctx.db(), path, bpm, std::move(keyRoot), totalBeats, beatMode,
                                     std::move(warpMarkers));
    if (ok) {
        ctx.bumpMediaRevision();
    }
    return ok;
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
        prefix.push_back(static_cast<char>(std::filesystem::path::preferred_separator));
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
        prefix.push_back(static_cast<char>(std::filesystem::path::preferred_separator));
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

bool rebuildFts(sqlite3* handle) {
    if (sqlite3_exec(handle, "INSERT INTO media_fts(media_fts) VALUES('delete-all')", nullptr,
                     nullptr, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_stmt* sel = nullptr;
    if (sqlite3_prepare_v2(handle,
                           "SELECT id, path, display_name, "
                           "(SELECT GROUP_CONCAT(tag, ' ') FROM media_tag "
                           " WHERE file_id = media_file.id) "
                           "FROM media_file",
                           -1, &sel, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_stmt* ins = nullptr;
    if (sqlite3_prepare_v2(handle,
                           "INSERT INTO media_fts (rowid, path_text, tag_text) VALUES (?, ?, ?)",
                           -1, &ins, nullptr) != SQLITE_OK) {
        sqlite3_finalize(sel);
        return false;
    }

    bool ok = true;
    while (sqlite3_step(sel) == SQLITE_ROW) {
        const auto id = sqlite3_column_int64(sel, 0);
        const std::string path = reinterpret_cast<const char*>(sqlite3_column_text(sel, 1));
        std::string pathText = pathTextFor(path);
        if (const auto* display = sqlite3_column_text(sel, 2)) {
            pathText += ' ';
            pathText += pathTextFor(reinterpret_cast<const char*>(display));
        }
        std::string tagText;
        if (const auto* tags = sqlite3_column_text(sel, 3)) {
            tagText = reinterpret_cast<const char*>(tags);
        }

        sqlite3_bind_int64(ins, 1, id);
        sqlite3_bind_text(ins, 2, pathText.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 3, tagText.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(ins) != SQLITE_DONE) {
            ok = false;
            break;
        }
        sqlite3_reset(ins);
        sqlite3_clear_bindings(ins);
    }
    sqlite3_finalize(sel);
    sqlite3_finalize(ins);
    return ok;
}

}  // namespace

bool updateEditableMediaRow(MediaDatabase& db, const EditableMediaRow& row) {
    if (row.fileId < 0 || row.path.empty()) {
        return false;
    }

    auto* handle = db.handle();
    if (sqlite3_exec(handle, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) {
        return false;
    }

    bool ok = true;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
                           "UPDATE media_file SET display_name = ?, family = ?, shape = ?, "
                           "bpm_user = ?, key_root_user = ?, key_scale_user = ?, duration_s = ? "
                           "WHERE id = ?",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        ok = false;
    } else {
        if (row.displayName && !row.displayName->empty()) {
            sqlite3_bind_text(stmt, 1, row.displayName->c_str(), -1, SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(stmt, 1);
        }
        const std::string family = row.family.empty() ? "unknown" : row.family;
        const std::string shape = row.shape.empty() ? "unknown" : row.shape;
        sqlite3_bind_text(stmt, 2, family.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, shape.c_str(), -1, SQLITE_TRANSIENT);
        if (row.bpm) {
            sqlite3_bind_double(stmt, 4, *row.bpm);
        } else {
            sqlite3_bind_null(stmt, 4);
        }
        if (row.keyRoot && !row.keyRoot->empty()) {
            sqlite3_bind_text(stmt, 5, row.keyRoot->c_str(), -1, SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(stmt, 5);
        }
        if (row.keyScale && !row.keyScale->empty()) {
            sqlite3_bind_text(stmt, 6, row.keyScale->c_str(), -1, SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(stmt, 6);
        }
        if (row.durationS) {
            sqlite3_bind_double(stmt, 7, *row.durationS);
        } else {
            sqlite3_bind_null(stmt, 7);
        }
        sqlite3_bind_int64(stmt, 8, row.fileId);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    }
    sqlite3_finalize(stmt);

    if (ok) {
        sqlite3_stmt* del = nullptr;
        if (sqlite3_prepare_v2(handle, "DELETE FROM media_tag WHERE file_id = ?", -1, &del,
                               nullptr) != SQLITE_OK) {
            ok = false;
        } else {
            sqlite3_bind_int64(del, 1, row.fileId);
            ok = sqlite3_step(del) == SQLITE_DONE;
        }
        sqlite3_finalize(del);
    }

    if (ok && !row.tags.empty()) {
        sqlite3_stmt* ins = nullptr;
        if (sqlite3_prepare_v2(
                handle,
                "INSERT INTO media_tag "
                "(file_id, tag, confidence, source_model) VALUES (?, ?, 1.0, 'user')",
                -1, &ins, nullptr) != SQLITE_OK) {
            ok = false;
        } else {
            for (const auto& tag : row.tags) {
                sqlite3_bind_int64(ins, 1, row.fileId);
                sqlite3_bind_text(ins, 2, tag.c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(ins) != SQLITE_DONE) {
                    ok = false;
                    break;
                }
                sqlite3_reset(ins);
                sqlite3_clear_bindings(ins);
            }
        }
        sqlite3_finalize(ins);
    }

    if (ok) {
        ok = rebuildFts(handle);
    }

    sqlite3_exec(handle, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
    return ok;
}

int updateEditableMediaRows(MediaDatabase& db, const std::vector<std::int64_t>& fileIds,
                            const BulkEditableMediaUpdate& update) {
    auto* handle = db.handle();
    if (fileIds.empty() ||
        sqlite3_exec(handle, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) {
        return 0;
    }

    int updatedRows = 0;
    bool ok = true;
    sqlite3_stmt* upd = nullptr;
    ok = sqlite3_prepare_v2(handle,
                            "UPDATE media_file SET "
                            "family = COALESCE(?, family), "
                            "shape = COALESCE(?, shape), "
                            "bpm_user = COALESCE(?, bpm_user), "
                            "key_root_user = COALESCE(?, key_root_user), "
                            "key_scale_user = COALESCE(?, key_scale_user), "
                            "duration_s = COALESCE(?, duration_s) "
                            "WHERE id = ?",
                            -1, &upd, nullptr) == SQLITE_OK;

    sqlite3_stmt* delTags = nullptr;
    sqlite3_stmt* insTag = nullptr;
    if (ok && update.tags) {
        ok = sqlite3_prepare_v2(handle, "DELETE FROM media_tag WHERE file_id = ?", -1, &delTags,
                                nullptr) == SQLITE_OK &&
             sqlite3_prepare_v2(
                 handle,
                 "INSERT INTO media_tag "
                 "(file_id, tag, confidence, source_model) VALUES (?, ?, 1.0, 'user')",
                 -1, &insTag, nullptr) == SQLITE_OK;
    }

    for (auto fileId : fileIds) {
        if (!ok) {
            break;
        }

        if (update.family && !update.family->empty()) {
            sqlite3_bind_text(upd, 1, update.family->c_str(), -1, SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(upd, 1);
        }
        if (update.shape && !update.shape->empty()) {
            sqlite3_bind_text(upd, 2, update.shape->c_str(), -1, SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(upd, 2);
        }
        if (update.bpm) {
            sqlite3_bind_double(upd, 3, *update.bpm);
        } else {
            sqlite3_bind_null(upd, 3);
        }
        if (update.keyRoot && !update.keyRoot->empty()) {
            sqlite3_bind_text(upd, 4, update.keyRoot->c_str(), -1, SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(upd, 4);
        }
        if (update.keyScale && !update.keyScale->empty()) {
            sqlite3_bind_text(upd, 5, update.keyScale->c_str(), -1, SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(upd, 5);
        }
        if (update.durationS) {
            sqlite3_bind_double(upd, 6, *update.durationS);
        } else {
            sqlite3_bind_null(upd, 6);
        }
        sqlite3_bind_int64(upd, 7, fileId);
        if (sqlite3_step(upd) == SQLITE_DONE) {
            updatedRows += sqlite3_changes(handle);
        } else {
            ok = false;
            break;
        }
        sqlite3_reset(upd);
        sqlite3_clear_bindings(upd);

        if (update.tags) {
            sqlite3_bind_int64(delTags, 1, fileId);
            if (sqlite3_step(delTags) != SQLITE_DONE) {
                ok = false;
                break;
            }
            sqlite3_reset(delTags);
            sqlite3_clear_bindings(delTags);

            for (const auto& tag : *update.tags) {
                sqlite3_bind_int64(insTag, 1, fileId);
                sqlite3_bind_text(insTag, 2, tag.c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(insTag) != SQLITE_DONE) {
                    ok = false;
                    break;
                }
                sqlite3_reset(insTag);
                sqlite3_clear_bindings(insTag);
            }
        }
    }

    sqlite3_finalize(upd);
    sqlite3_finalize(delTags);
    sqlite3_finalize(insTag);
    if (ok) {
        ok = rebuildFts(handle);
    }
    sqlite3_exec(handle, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
    return ok ? updatedRows : 0;
}

int resetMediaRowsToDetected(MediaDatabase& db, const std::vector<std::int64_t>& fileIds) {
    auto* handle = db.handle();
    if (fileIds.empty() ||
        sqlite3_exec(handle, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) {
        return 0;
    }

    sqlite3_stmt* stmt = nullptr;
    bool ok = sqlite3_prepare_v2(handle,
                                 "UPDATE media_file SET "
                                 "bpm_user = NULL, "
                                 "key_root_user = NULL, "
                                 "key_scale_user = NULL, "
                                 "total_beats_user = NULL, "
                                 "beat_mode_user = NULL, "
                                 "warp_markers_json = NULL "
                                 "WHERE id = ?",
                                 -1, &stmt, nullptr) == SQLITE_OK;
    int updatedRows = 0;
    for (auto fileId : fileIds) {
        if (!ok) {
            break;
        }
        sqlite3_bind_int64(stmt, 1, fileId);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            updatedRows += sqlite3_changes(handle);
        } else {
            ok = false;
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }
    sqlite3_finalize(stmt);
    sqlite3_exec(handle, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
    return ok ? updatedRows : 0;
}

std::vector<MissingFileCandidate> findMissingFileCandidates(MediaDatabase& db, std::int64_t fileId,
                                                            int limit) {
    std::vector<MissingFileCandidate> out;
    if (fileId < 0 || limit <= 0) {
        return out;
    }

    auto* handle = db.handle();
    sqlite3_stmt* target = nullptr;
    if (sqlite3_prepare_v2(handle,
                           "SELECT path, size_bytes, content_hash FROM media_file WHERE id = ?", -1,
                           &target, nullptr) != SQLITE_OK) {
        return out;
    }
    sqlite3_bind_int64(target, 1, fileId);
    if (sqlite3_step(target) != SQLITE_ROW) {
        sqlite3_finalize(target);
        return out;
    }

    const auto targetPath =
        std::filesystem::path(reinterpret_cast<const char*>(sqlite3_column_text(target, 0)));
    const auto targetName = targetPath.filename().string();
    const auto targetSize = sqlite3_column_int64(target, 1);
    const void* hashBlob = sqlite3_column_blob(target, 2);
    const int hashBytes = sqlite3_column_bytes(target, 2);
    std::vector<std::uint8_t> targetHash;
    if (hashBlob != nullptr && hashBytes > 0) {
        const auto* bytes = static_cast<const std::uint8_t*>(hashBlob);
        targetHash.assign(bytes, bytes + hashBytes);
    }
    sqlite3_finalize(target);

    std::unordered_map<std::int64_t, bool> seen;
    auto addCandidate = [&](std::int64_t candidateId, const char* pathText, std::int64_t sizeBytes,
                            const char* reason) {
        if (candidateId == fileId || pathText == nullptr || seen.count(candidateId) != 0 ||
            static_cast<int>(out.size()) >= limit) {
            return;
        }
        std::filesystem::path candidatePath(pathText);
        std::error_code ec;
        if (!std::filesystem::exists(candidatePath, ec) || ec) {
            return;
        }
        seen.emplace(candidateId, true);
        out.push_back({candidateId, std::move(candidatePath), reason, sizeBytes});
    };

    if (!targetHash.empty()) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(handle,
                               "SELECT id, path, size_bytes FROM media_file "
                               "WHERE id != ? AND content_hash = ? "
                               "ORDER BY indexed_at DESC LIMIT ?",
                               -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, fileId);
            sqlite3_bind_blob(stmt, 2, targetHash.data(), static_cast<int>(targetHash.size()),
                              SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 3, limit);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                addCandidate(sqlite3_column_int64(stmt, 0),
                             reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)),
                             sqlite3_column_int64(stmt, 2), "content hash");
            }
        }
        sqlite3_finalize(stmt);
    }

    if (static_cast<int>(out.size()) >= limit || targetName.empty()) {
        return out;
    }

    const auto tolerance = std::max<std::int64_t>(4096, targetSize / 50);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(handle,
                           "SELECT id, path, size_bytes FROM media_file "
                           "WHERE id != ? AND size_bytes BETWEEN ? AND ? "
                           "ORDER BY indexed_at DESC LIMIT 200",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return out;
    }
    sqlite3_bind_int64(stmt, 1, fileId);
    sqlite3_bind_int64(stmt, 2, std::max<std::int64_t>(0, targetSize - tolerance));
    sqlite3_bind_int64(stmt, 3, targetSize + tolerance);
    while (sqlite3_step(stmt) == SQLITE_ROW && static_cast<int>(out.size()) < limit) {
        const auto* pathText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (pathText == nullptr) {
            continue;
        }
        if (std::filesystem::path(pathText).filename().string() != targetName) {
            continue;
        }
        addCandidate(sqlite3_column_int64(stmt, 0), pathText, sqlite3_column_int64(stmt, 2),
                     "same name + close size");
    }
    sqlite3_finalize(stmt);
    return out;
}

bool recoverMissingMediaFilePath(MediaDatabase& db, std::int64_t fileId,
                                 const std::filesystem::path& newPath) {
    if (fileId < 0 || newPath.empty()) {
        return false;
    }

    auto* handle = db.handle();
    if (sqlite3_exec(handle, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) {
        return false;
    }

    const auto pathText = newPath.string();
    std::int64_t duplicateId = -1;
    sqlite3_stmt* dup = nullptr;
    bool ok = true;
    if (sqlite3_prepare_v2(handle, "SELECT id FROM media_file WHERE path = ?", -1, &dup, nullptr) ==
        SQLITE_OK) {
        sqlite3_bind_text(dup, 1, pathText.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(dup) == SQLITE_ROW) {
            duplicateId = sqlite3_column_int64(dup, 0);
        }
    } else {
        ok = false;
    }
    sqlite3_finalize(dup);

    if (ok && duplicateId == fileId) {
        sqlite3_exec(handle, "COMMIT", nullptr, nullptr, nullptr);
        return true;
    }

    if (ok && duplicateId >= 0) {
        sqlite3_stmt* copy = nullptr;
        ok = sqlite3_prepare_v2(
                 handle,
                 "UPDATE media_file SET "
                 "kind = (SELECT kind FROM media_file WHERE id = ?1), "
                 "format = (SELECT format FROM media_file WHERE id = ?1), "
                 "size_bytes = (SELECT size_bytes FROM media_file WHERE id = ?1), "
                 "mtime_ns = (SELECT mtime_ns FROM media_file WHERE id = ?1), "
                 "content_hash = (SELECT content_hash FROM media_file WHERE id = ?1), "
                 "indexed_at = (SELECT indexed_at FROM media_file WHERE id = ?1), "
                 "duration_s = (SELECT duration_s FROM media_file WHERE id = ?1), "
                 "sample_rate = (SELECT sample_rate FROM media_file WHERE id = ?1), "
                 "channels = (SELECT channels FROM media_file WHERE id = ?1), "
                 "bpm = (SELECT bpm FROM media_file WHERE id = ?1), "
                 "key_root = (SELECT key_root FROM media_file WHERE id = ?1), "
                 "key_scale = (SELECT key_scale FROM media_file WHERE id = ?1), "
                 "key_confidence = (SELECT key_confidence FROM media_file WHERE id = ?1), "
                 "rms = (SELECT rms FROM media_file WHERE id = ?1), "
                 "spectral_centroid = (SELECT spectral_centroid FROM media_file WHERE id = ?1), "
                 "spectral_flatness = (SELECT spectral_flatness FROM media_file WHERE id = ?1), "
                 "transient_density = (SELECT transient_density FROM media_file WHERE id = ?1), "
                 "shape = (SELECT shape FROM media_file WHERE id = ?1), "
                 "family = (SELECT family FROM media_file WHERE id = ?1), "
                 "tonal = (SELECT tonal FROM media_file WHERE id = ?1) "
                 "WHERE id = ?2",
                 -1, &copy, nullptr) == SQLITE_OK;
        if (ok) {
            sqlite3_bind_int64(copy, 1, duplicateId);
            sqlite3_bind_int64(copy, 2, fileId);
            ok = sqlite3_step(copy) == SQLITE_DONE;
        }
        sqlite3_finalize(copy);

        if (ok) {
            sqlite3_stmt* del = nullptr;
            ok = sqlite3_prepare_v2(handle, "DELETE FROM media_file WHERE id = ?", -1, &del,
                                    nullptr) == SQLITE_OK;
            if (ok) {
                sqlite3_bind_int64(del, 1, duplicateId);
                ok = sqlite3_step(del) == SQLITE_DONE;
            }
            sqlite3_finalize(del);
        }

        if (ok) {
            sqlite3_stmt* updPath = nullptr;
            ok = sqlite3_prepare_v2(handle, "UPDATE media_file SET path = ? WHERE id = ?", -1,
                                    &updPath, nullptr) == SQLITE_OK;
            if (ok) {
                sqlite3_bind_text(updPath, 1, pathText.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(updPath, 2, fileId);
                ok = sqlite3_step(updPath) == SQLITE_DONE && sqlite3_changes(handle) > 0;
            }
            sqlite3_finalize(updPath);
        }
    } else if (ok) {
        sqlite3_stmt* upd = nullptr;
        ok = sqlite3_prepare_v2(handle, "UPDATE media_file SET path = ? WHERE id = ?", -1, &upd,
                                nullptr) == SQLITE_OK;
        if (ok) {
            sqlite3_bind_text(upd, 1, pathText.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(upd, 2, fileId);
            ok = sqlite3_step(upd) == SQLITE_DONE && sqlite3_changes(handle) > 0;
        }
        sqlite3_finalize(upd);
    }

    if (ok) {
        ok = rebuildFts(handle);
    }

    sqlite3_exec(handle, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
    return ok;
}

int deleteMediaRows(MediaDatabase& db, const std::vector<std::int64_t>& fileIds) {
    auto* handle = db.handle();
    if (fileIds.empty() ||
        sqlite3_exec(handle, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) {
        return 0;
    }

    int removed = 0;
    bool ok = true;
    sqlite3_stmt* delFile = nullptr;
    ok = sqlite3_prepare_v2(handle, "DELETE FROM media_file WHERE id = ?", -1, &delFile, nullptr) ==
         SQLITE_OK;

    if (ok) {
        for (auto fileId : fileIds) {
            sqlite3_bind_int64(delFile, 1, fileId);
            if (sqlite3_step(delFile) == SQLITE_DONE) {
                removed += sqlite3_changes(handle);
            } else {
                ok = false;
                break;
            }
            sqlite3_reset(delFile);
            sqlite3_clear_bindings(delFile);
        }
    }

    sqlite3_finalize(delFile);
    if (ok) {
        ok = rebuildFts(handle);
    }
    sqlite3_exec(handle, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
    return ok ? removed : 0;
}

int removeDuplicateFilePathRows(MediaDatabase& db) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(
            db.handle(),
            "SELECT id, path, indexed_at, "
            "(bpm_user IS NOT NULL OR key_root_user IS NOT NULL OR "
            " key_scale_user IS NOT NULL OR total_beats_user IS NOT NULL OR "
            " beat_mode_user IS NOT NULL OR warp_markers_json IS NOT NULL OR "
            " display_name IS NOT NULL OR EXISTS (SELECT 1 FROM media_tag "
            " WHERE file_id = media_file.id AND source_model = 'user')) AS user_edited, "
            "EXISTS (SELECT 1 FROM media_embedding WHERE file_id = media_file.id) AS analyzed "
            "FROM media_file ORDER BY id",
            -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }

    std::unordered_map<std::string, FilePathDedupeRow> keeperByPhysicalFile;
    std::vector<std::int64_t> removeIds;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* pathText = sqlite3_column_text(stmt, 1);
        if (pathText == nullptr) {
            continue;
        }

        FilePathDedupeRow row;
        row.fileId = sqlite3_column_int64(stmt, 0);
        row.path = std::filesystem::path(reinterpret_cast<const char*>(pathText));
        row.indexedAt = sqlite3_column_int64(stmt, 2);
        row.userEdited = sqlite3_column_int(stmt, 3) != 0;
        row.analyzed = sqlite3_column_int(stmt, 4) != 0;

        const auto key = physicalFileKey(row.path);
        if (!key) {
            continue;
        }

        auto it = keeperByPhysicalFile.find(*key);
        if (it == keeperByPhysicalFile.end()) {
            keeperByPhysicalFile.emplace(*key, std::move(row));
            continue;
        }

        if (isBetterDuplicateKeeper(row, it->second)) {
            removeIds.push_back(it->second.fileId);
            it->second = std::move(row);
        } else {
            removeIds.push_back(row.fileId);
        }
    }
    sqlite3_finalize(stmt);

    if (removeIds.empty()) {
        return 0;
    }
    std::sort(removeIds.begin(), removeIds.end());
    removeIds.erase(std::unique(removeIds.begin(), removeIds.end()), removeIds.end());
    return deleteMediaRows(db, removeIds);
}

int moveFolderInLibrary(MediaDatabase& db, const std::filesystem::path& oldFolder,
                        const std::filesystem::path& newFolder) {
    std::string oldPrefix = oldFolder.string();
    std::string newPrefix = newFolder.string();
    if (oldPrefix.empty() || newPrefix.empty()) {
        juce::Logger::writeToLog("[moveFolder] empty prefix, aborting");
        return 0;
    }
    constexpr char kSep = static_cast<char>(std::filesystem::path::preferred_separator);
    if (oldPrefix.back() != '/' && oldPrefix.back() != '\\') {
        oldPrefix.push_back(kSep);
    }
    if (newPrefix.back() != '/' && newPrefix.back() != '\\') {
        newPrefix.push_back(kSep);
    }
    if (oldPrefix == newPrefix) {
        juce::Logger::writeToLog("[moveFolder] old == new (" + juce::String(oldPrefix) +
                                 "), nothing to do");
        return 0;
    }
    std::string upper = oldPrefix;
    upper.back() = static_cast<char>(static_cast<unsigned char>(upper.back()) + 1);

    auto* handle = db.handle();

    // Probe how many rows the WHERE actually matches so we can tell a real
    // "moved 0 rows" outcome from a range-bound bug like the one fixed in
    // the path-separator pass on Windows.
    int matchedRows = -1;
    {
        sqlite3_stmt* probe = nullptr;
        if (sqlite3_prepare_v2(
                handle, "SELECT COUNT(*) FROM media_file WHERE path >= ? AND path < ?", -1, &probe,
                nullptr) == SQLITE_OK) {
            sqlite3_bind_text(probe, 1, oldPrefix.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(probe, 2, upper.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(probe) == SQLITE_ROW) {
                matchedRows = sqlite3_column_int(probe, 0);
            }
            sqlite3_finalize(probe);
        }
    }
    juce::Logger::writeToLog("[moveFolder] oldPrefix='" + juce::String(oldPrefix) +
                             "' newPrefix='" + juce::String(newPrefix) + "' upper='" +
                             juce::String(upper) + "' matched=" + juce::String(matchedRows));

    if (sqlite3_exec(handle, "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK) {
        juce::Logger::writeToLog("[moveFolder] BEGIN failed");
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
        juce::Logger::writeToLog("[moveFolder] rolling back");
        sqlite3_exec(handle, "ROLLBACK", nullptr, nullptr, nullptr);
        return -1;
    }
    sqlite3_exec(handle, "COMMIT", nullptr, nullptr, nullptr);
    juce::Logger::writeToLog("[moveFolder] committed, rows=" + juce::String(updatedRows));
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
