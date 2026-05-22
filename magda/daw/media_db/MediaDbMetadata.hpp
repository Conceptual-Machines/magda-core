// Two-way binding between the media DB and tracktion clips for
// user-editable per-file metadata (issue #768).
//
// The DB stores two slots per editable property: the scanner-detected
// value (e.g. `bpm`) and the user override (`bpm_user`). The user-facing
// "effective" value is COALESCE(user, detected) — so when the user edits
// a clip's BPM in MAGDA the override is written to the user slot, while
// the scanner's detected value stays as a fallback for files the user
// hasn't edited.
//
// This header exposes the read/write API. The read path is used by the
// drop hooks (file → clip). The write path is used by a ValueTree
// listener attached to clips that watches for property changes.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace magda::media {

class MediaDatabase;

struct EffectiveMetadata {
    std::optional<double> bpm;
    std::optional<std::string> keyRoot;
    std::optional<std::string> keyScale;
    std::optional<double> totalBeats;
    std::optional<bool> beatMode;
};

struct WarpMarkerMetadata {
    double sourceSec = 0.0;
    double beat = 0.0;
};

struct EditableMediaRow {
    std::int64_t fileId = -1;
    std::filesystem::path path;
    std::optional<std::string> displayName;
    std::string family;
    std::string shape;
    std::optional<double> bpm;
    std::optional<std::string> keyRoot;
    std::optional<std::string> keyScale;
    std::optional<double> durationS;
    std::vector<std::string> tags;
};

struct BulkEditableMediaUpdate {
    std::optional<std::string> family;
    std::optional<std::string> shape;
    std::optional<double> bpm;
    std::optional<std::string> keyRoot;
    std::optional<std::string> keyScale;
    std::optional<double> durationS;
    std::optional<std::vector<std::string>> tags;
};

struct MissingFileCandidate {
    std::int64_t fileId = -1;
    std::filesystem::path path;
    std::string matchReason;
    std::int64_t sizeBytes = 0;
};

// Look up a file by absolute path. Returns nullopt if the file isn't in
// the media DB. Each field is COALESCE(user_override, detected) so the
// caller gets a single effective value per property.
[[nodiscard]] std::optional<EffectiveMetadata> getEffectiveMetadata(
    MediaDatabase& db, const std::filesystem::path& path);

// Read only explicit user-saved overrides. Unlike getEffectiveMetadata(), this
// does not fall back to scanner values, so clip import can restore intentional
// saves without adopting tagger guesses as source interpretation.
[[nodiscard]] std::optional<EffectiveMetadata> getUserMetadata(MediaDatabase& db,
                                                               const std::filesystem::path& path);

// Write a user override. Pass nullopt to clear the override (effective
// value falls back to the scanner-detected one). No-op if the file isn't
// in the DB — the metadata API does not create rows.
void setUserBpm(MediaDatabase& db, const std::filesystem::path& path, std::optional<double> bpm);

void setUserKey(MediaDatabase& db, const std::filesystem::path& path,
                std::optional<std::string> root, std::optional<std::string> scale);

// Update only the key root override, leaving key_scale_user untouched.
// Used by inspector UI that exposes root but not scale - blowing away
// the scale override every time the user picks a new root would be
// surprising.
void setUserKeyRoot(MediaDatabase& db, const std::filesystem::path& path,
                    std::optional<std::string> root);

[[nodiscard]] bool isFileIndexed(MediaDatabase& db, const std::filesystem::path& path);

[[nodiscard]] std::optional<EditableMediaRow> getEditableMediaRow(MediaDatabase& db,
                                                                  std::int64_t fileId);

bool updateEditableMediaRow(MediaDatabase& db, const EditableMediaRow& row);

int updateEditableMediaRows(MediaDatabase& db, const std::vector<std::int64_t>& fileIds,
                            const BulkEditableMediaUpdate& update);

int resetMediaRowsToDetected(MediaDatabase& db, const std::vector<std::int64_t>& fileIds);

[[nodiscard]] std::vector<MissingFileCandidate> findMissingFileCandidates(MediaDatabase& db,
                                                                          std::int64_t fileId,
                                                                          int limit = 12);

bool recoverMissingMediaFilePath(MediaDatabase& db, std::int64_t fileId,
                                 const std::filesystem::path& newPath);

int deleteMediaRows(MediaDatabase& db, const std::vector<std::int64_t>& fileIds);

// Remove duplicate DB rows that resolve to the same physical file on disk
// (same device + inode). Exact duplicate path strings are already blocked by
// media_file.path UNIQUE, so this handles symlink/alias/case variants. Files
// on disk are not deleted. Returns removed media_file rows.
int removeDuplicateFilePathRows(MediaDatabase& db);

[[nodiscard]] std::optional<std::vector<WarpMarkerMetadata>> getUserWarpMarkers(
    MediaDatabase& db, const std::filesystem::path& path);

void setUserWarpMarkers(MediaDatabase& db, const std::filesystem::path& path,
                        std::optional<std::vector<WarpMarkerMetadata>> markers);

[[nodiscard]] bool saveUserMetadata(MediaDatabase& db, const std::filesystem::path& path,
                                    std::optional<double> bpm, std::optional<std::string> keyRoot,
                                    std::optional<double> totalBeats, std::optional<bool> beatMode,
                                    std::optional<std::vector<WarpMarkerMetadata>> warpMarkers);

// Convenience overloads that go through the singleton MediaDbContext.
// They open the DB lazily on first call (no-op if already open) and
// silently no-op on init failure or when the file isn't indexed. UI code
// can use these without holding a database handle of its own.
[[nodiscard]] std::optional<EffectiveMetadata> getEffectiveMetadataForFile(
    const std::filesystem::path& path);

[[nodiscard]] std::optional<EffectiveMetadata> getUserMetadataForFile(
    const std::filesystem::path& path);

void setUserBpmForFile(const std::filesystem::path& path, std::optional<double> bpm);

void setUserKeyForFile(const std::filesystem::path& path, std::optional<std::string> root,
                       std::optional<std::string> scale);

void setUserKeyRootForFile(const std::filesystem::path& path, std::optional<std::string> root);

[[nodiscard]] bool isFileIndexed(const std::filesystem::path& path);

[[nodiscard]] std::optional<std::vector<WarpMarkerMetadata>> getUserWarpMarkersForFile(
    const std::filesystem::path& path);

[[nodiscard]] bool saveUserMetadataForFile(
    const std::filesystem::path& path, std::optional<double> bpm,
    std::optional<std::string> keyRoot, std::optional<double> totalBeats,
    std::optional<bool> beatMode, std::optional<std::vector<WarpMarkerMetadata>> warpMarkers);

// True if the DB has any indexed file under `folder` (descendant in the
// path hierarchy, not equal to it). Used by the file-browser folder
// right-click menu to label its action "Index" vs "Re-index" depending
// on whether the folder has been scanned before.
//
// Implementation uses a range query (path >= folder/, path < folder0)
// against the unique-index on media_file.path, so it's O(log N) even on
// libraries with hundreds of thousands of files — much faster than a
// LIKE-prefix scan.
[[nodiscard]] bool hasIndexedDescendant(MediaDatabase& db, const std::filesystem::path& folder);

[[nodiscard]] bool hasIndexedDescendantOfFolder(const std::filesystem::path& folder);

// Delete every indexed row whose path lives under `folder`. Cascades drop
// the associated media_tag / media_embedding / media_metadata entries via
// ON DELETE CASCADE; media_fts is contentless and gets an explicit DELETE
// in the same transaction. Returns the number of media_file rows removed.
// Same range-query semantics as hasIndexedDescendant.
int removeFolderFromLibrary(MediaDatabase& db, const std::filesystem::path& folder);

int removeFolderFromLibrary(const std::filesystem::path& folder);

// Rewrite media_file.path (and media_fts.path_text) so every row under
// `oldFolder` ends up rooted at `newFolder`. Useful when the user has
// physically moved a sample folder on disk and wants the library to
// follow without re-indexing. Does not move any files on disk. Returns
// the number of media_file rows updated; 0 on no-op (no descendants).
// Both transactional: a failed UPDATE (e.g. UNIQUE collision with rows
// already living at the new prefix) rolls the whole thing back and
// returns -1.
int moveFolderInLibrary(MediaDatabase& db, const std::filesystem::path& oldFolder,
                        const std::filesystem::path& newFolder);

int moveFolderInLibrary(const std::filesystem::path& oldFolder,
                        const std::filesystem::path& newFolder);

}  // namespace magda::media
