// PresetDbIndexer — walks MAGDA's on-disk presets folder and mirrors
// each .mps file into the media DB as a kind='preset' row. Used both
// for the "Index presets" Preferences action (indexAll) and for
// per-save hooks in PresetManager (upsertOne / removeOne).
//
// No audio decoding, no CLAP, no embeddings. Presets are small JSON
// files; the indexer parses the envelope's kind field cheaply and
// writes path/mtime/size and a basenames-based FTS row.

#pragma once

#include <filesystem>
#include <string>

namespace magda::media {

class MediaDatabase;

class PresetDbIndexer {
  public:
    struct Stats {
        int inserted = 0;
        int updated = 0;
        int skipped = 0;
        int failed = 0;
    };

    explicit PresetDbIndexer(MediaDatabase& db);

    // Walk `presetsRoot/{Chains,Racks,Devices}` and upsert every .mps
    // file into media_file. Existing rows whose (mtime_ns, size_bytes)
    // already match the on-disk file are skipped.
    Stats indexAll(const std::filesystem::path& presetsRoot);

    // Upsert a single .mps file. The preset_kind is derived from the
    // path's first segment under `presetsRoot` (Chains/Racks/Devices),
    // so this only needs the absolute file path. Returns true if the
    // row was written or skipped-unchanged, false on I/O / SQL error.
    bool upsertOne(const std::filesystem::path& presetsRoot, const std::filesystem::path& path);

    // Remove the media_file row (and FTS entry) for the given path.
    // No-op if no such row exists. Returns true on success.
    bool removeOne(const std::filesystem::path& path);

  private:
    MediaDatabase& db_;
};

}  // namespace magda::media
