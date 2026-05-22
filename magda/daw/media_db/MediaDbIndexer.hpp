// Orchestrator that walks a directory and populates the media DB
// (issue #768). One indexer per scan; caller owns threading.
//
// Pipeline per file:
//   scan::classify -> hash(first 1MiB) -> skip if unchanged ->
//   AudioFeatures::extract -> PathRules::pathFamilyHint/pathTags/
//   parseBpmFromPath/parseKeyFromPath -> derive shape/family/tonal ->
//   policy rules (one-shot has no BPM, drum/fx has no key) ->
//   write media_file + media_tag + media_fts row.
//
// The encoder is intentionally nullable — when "AI Audio Pack" isn't
// installed we still index path tags, features, and FTS keyword search. The
// slower semantic embedding pass runs separately after the scan and
// gracefully does nothing without an encoder.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace magda::media {

class MediaDatabase;
class ClapAudioEncoder;
class ClapTextEncoder;
class RobertaTokenizer;
class ZeroShotTagger;

class MediaDbIndexer {
  public:
    struct Stats {
        int inserted = 0;  // first time we've seen the path
        int updated = 0;   // path known but content/mtime changed
        int skipped = 0;   // path known and unchanged
        int failed = 0;    // unreadable or decode-failed
    };
    struct EmbeddingStats {
        int embedded = 0;  // embedding row written
        int skipped = 0;   // no encoder or no missing rows
        int failed = 0;    // decode/inference/write failed
    };
    struct ScanTagOptions {
        std::vector<std::string> customTags;
        bool includeRootFolderName = false;
        bool includePathNodes = false;
        std::filesystem::path root;
    };

    // Progress callback fired once per file. (done, total, currentPath)
    // where total is the prescanned file count and currentPath is the file
    // that was just processed. In parallel mode the callback may be invoked
    // from any worker thread; calls are serialised behind an internal mutex
    // so the callee sees one at a time, but the callee is still responsible
    // for marshalling to the UI thread if it touches UI.
    using ProgressFn =
        std::function<void(int done, int total, const std::filesystem::path& current)>;
    using FailureFn =
        std::function<void(const std::filesystem::path& current, const std::string& reason)>;
    using ShouldCancelFn = std::function<bool()>;

    // What the scan does with rows that already exist in media_file.
    enum class Mode {
        // Default: re-derive any file whose (mtime, size, hash) has drifted
        // from what we recorded last time. Unchanged files are skipped
        // cheaply. This is the "Index this folder" / first-time scan.
        Incremental,
        // Skip every file whose path is already in the DB — even when its
        // content has changed. Only previously-unseen files get processed.
        // This is the "Scan for new files" action.
        OnlyNew,
        // Re-derive everything regardless of any cached state. Used by the
        // "Re-index" action when rules / algorithms have changed and the
        // user wants the existing rows refreshed.
        ForceAll,
    };

    using TextEncoderProvider = std::function<ClapTextEncoder*()>;
    using TokenizerProvider = std::function<RobertaTokenizer*()>;

    // db: required. encoder: nullable, controls whether the post-scan
    // embedding backfill can run. textEncoderProvider + tokenizerProvider:
    // optional, each returns the corresponding model when called. The
    // indexer only invokes the providers when an embedding pass actually
    // has pending files, so installing the bundle doesn't make ordinary
    // indexing scans pay the ~480 MB text-model load cost. Providers
    // returning null (or providers themselves left empty) disable the
    // CLAP zero-shot tagging side of the embedding pass while still
    // producing audio embeddings (issue #1319).
    MediaDbIndexer(MediaDatabase& db, ClapAudioEncoder* encoder,
                   TextEncoderProvider textEncoderProvider = {},
                   TokenizerProvider tokenizerProvider = {});
    ~MediaDbIndexer();
    MediaDbIndexer(const MediaDbIndexer&) = delete;
    MediaDbIndexer& operator=(const MediaDbIndexer&) = delete;
    MediaDbIndexer(MediaDbIndexer&&) = delete;
    MediaDbIndexer& operator=(MediaDbIndexer&&) = delete;

    void setProgress(ProgressFn fn);
    void setFailureCallback(FailureFn fn);
    void setShouldCancel(ShouldCancelFn fn);
    void setScanTagOptions(ScanTagOptions options);

    // Synchronously walk `root`, indexing every classifying file.
    //
    // numThreads:
    //   0  → auto: max(1, hardware_concurrency() - 1), leaves a core free.
    //   1  → serial, each changed file gets a short write transaction.
    //   >1 → parallel: each worker opens its own MediaDatabase against the
    //        same file under WAL, pulls batches off an atomic work-queue,
    //        and writes changed files with short per-file transactions.
    //
    // Forced to 1 automatically when the DB is in-memory (workers can't
    // share state across connections) or when the scan has fewer than ~64
    // files (setup cost dominates).
    Stats indexDirectory(const std::filesystem::path& root, int numThreads = 0,
                         Mode mode = Mode::Incremental);
    Stats indexFile(const std::filesystem::path& path, Mode mode = Mode::ForceAll);
    Stats indexFileIds(const std::vector<std::int64_t>& fileIds, Mode mode = Mode::ForceAll);

    // Backfill semantic embeddings for indexed audio rows under `root` that
    // are missing the current encoder's model/version row. This is separate
    // from indexDirectory so the library becomes browsable before slow CLAP
    // work finishes.
    EmbeddingStats embedMissingAudio(const std::filesystem::path& root = {});
    EmbeddingStats embedAudioFileIds(const std::vector<std::int64_t>& fileIds);

  private:
    MediaDatabase& db_;
    ClapAudioEncoder* encoder_;
    TextEncoderProvider textEncoderProvider_;
    TokenizerProvider tokenizerProvider_;
    // Built lazily on the first embedding pass that actually has pending
    // files. Held as a unique_ptr so the header doesn't have to pull in
    // MediaDbZeroShotTags.hpp.
    std::unique_ptr<ZeroShotTagger> zeroShotTagger_;
    ProgressFn progress_;
    FailureFn failure_;
    ShouldCancelFn shouldCancel_;
    ScanTagOptions scanTagOptions_;
};

}  // namespace magda::media
