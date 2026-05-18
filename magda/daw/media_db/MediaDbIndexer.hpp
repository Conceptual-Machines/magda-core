// Orchestrator that walks a directory and populates the media DB
// (issue #768). One indexer per scan; caller owns threading.
//
// Pipeline per file:
//   scan::classify -> hash(first 1MiB) -> skip if unchanged ->
//   AudioFeatures::extract -> PathRules::pathFamilyHint/pathTags/
//   parseBpmFromPath/parseKeyFromPath -> derive shape/family/tonal ->
//   policy rules (one-shot has no BPM, drum/fx has no key) ->
//   ClapAudioEncoder::embed (optional) -> write media_file +
//   media_tag (CLAP + path source) + media_embedding (if encoder) +
//   media_fts row.
//
// The encoder is intentionally nullable — when "AI Audio Pack" isn't
// installed we still index path tags, features, and FTS keyword search.
// Semantic search just gracefully unavailable in that mode.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>

namespace magda::media {

class MediaDatabase;
class ClapAudioEncoder;

class MediaDbIndexer {
  public:
    struct Stats {
        int inserted = 0;  // first time we've seen the path
        int updated = 0;   // path known but content/mtime changed
        int skipped = 0;   // path known and unchanged
        int failed = 0;    // unreadable or decode-failed
    };

    // Progress callback fired once per file. (done, total, currentPath)
    // where total is the prescanned file count and currentPath is the file
    // that was just processed. In parallel mode the callback may be invoked
    // from any worker thread; calls are serialised behind an internal mutex
    // so the callee sees one at a time, but the callee is still responsible
    // for marshalling to the UI thread if it touches UI.
    using ProgressFn =
        std::function<void(int done, int total, const std::filesystem::path& current)>;

    // db: required. encoder: nullable, controls whether we compute audio
    // embeddings during this scan.
    MediaDbIndexer(MediaDatabase& db, ClapAudioEncoder* encoder);

    void setProgress(ProgressFn fn);

    // Synchronously walk `root`, indexing every classifying file.
    //
    // numThreads:
    //   0  → auto: max(1, hardware_concurrency() - 1), leaves a core free.
    //   1  → serial, single transaction wraps the whole walk.
    //   >1 → parallel: each worker opens its own MediaDatabase against the
    //        same file under WAL, pulls batches off an atomic work-queue,
    //        and commits per batch (~50 files).
    //
    // Forced to 1 automatically when the DB is in-memory (workers can't
    // share state across connections) or when the scan has fewer than ~64
    // files (setup cost dominates).
    //
    // force: when true, bypass the skip-on-unchanged fast path and
    // re-derive everything (path tags, family, features, embedding) for
    // every file. Used by the "Re-index folder" menu item to pick up new
    // keywords / improved algorithms on already-indexed content.
    Stats indexDirectory(const std::filesystem::path& root, int numThreads = 0, bool force = false);

  private:
    MediaDatabase& db_;
    ClapAudioEncoder* encoder_;
    ProgressFn progress_;
};

}  // namespace magda::media
