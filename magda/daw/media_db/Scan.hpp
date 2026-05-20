// File system walker + extension-based kind classification (issue #768).
//
// Mirrors prototypes/media_db/src/media_db/scan.py. The C++ runtime keeps
// the same kind vocabulary ("audio" | "preset" | "clip") and the same
// extension table so a DB written by the prototype is interoperable.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace magda::media {

struct ScannedFile {
    std::filesystem::path path;
    std::string kind;        // "audio" | "preset" | "clip"
    std::string format;      // file extension, lowercase, no leading dot
    std::int64_t sizeBytes;  // follows symlinks
    std::int64_t mtimeNs;    // ns since epoch (matches Python's st_mtime_ns)
};

// Classify a single file. Returns std::nullopt for unknown extensions or
// when stat fails (file disappeared, permission denied, symlink to nowhere).
// Symlinks are followed for stat info.
std::optional<ScannedFile> classify(const std::filesystem::path& path);

// Walk root recursively and call visit() for each file that classifies.
// Skips non-classifying files silently. Permission errors on individual
// directories are skipped rather than thrown — a missing subtree shouldn't
// abort the whole index. Directory symlinks are followed (users symlink
// external sample drives into their library root).
void walk(const std::filesystem::path& root, const std::function<void(const ScannedFile&)>& visit);

}  // namespace magda::media
