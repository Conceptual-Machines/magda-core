// App-wide owner of the media database state (issue #768).
//
// Lazily opens the SQLite file under MAGDA's data dir on first access,
// and tries to load the CLAP encoders + RoBERTa tokenizer from the
// adjacent "models" subdirectory. Missing model files are not fatal —
// the rest of the app gets nullable encoders so search degrades to
// "FTS BM25 + filters" (the no-AI-pack story).
//
// Singleton pattern matches LlamaModelManager / AudioThumbnailManager:
// long-lived for the app's lifetime, init costs paid once.

#pragma once

#include <atomic>
#include <filesystem>
#include <memory>

namespace magda::media {

class MediaDatabase;
class ClapAudioEncoder;
class ClapTextEncoder;
class RobertaTokenizer;

class MediaDbContext {
  public:
    static MediaDbContext& getInstance();

    // Idempotent. Returns true if the database is open. Encoder + tokenizer
    // status is reported separately via the accessors below.
    bool ensureInitialized();

    // Reset everything (closes DB + drops encoders). Mainly useful in tests.
    void shutdown();

    [[nodiscard]] bool isReady() const noexcept;
    [[nodiscard]] bool hasAudioEncoder() const noexcept;
    [[nodiscard]] bool hasTextSearch() const noexcept;  // tokenizer + textEncoder both loaded

    // Runtime "loaded into memory" check — distinct from the file-on-disk
    // checks above. True once the underlying ORT session / tokenizer has
    // been instantiated (whether by lazy access or explicit preload).
    [[nodiscard]] bool isAudioEncoderLoaded() const noexcept;
    [[nodiscard]] bool isTextEncoderLoaded() const noexcept;
    [[nodiscard]] bool isTokenizerLoaded() const noexcept;

    // True while at least one model is currently being instantiated
    // (preloadModels in progress, or a lazy accessor mid-construction on
    // some other thread). Polled by the DB browser's status indicator.
    [[nodiscard]] bool isLoadInProgress() const noexcept;

    // Force the lazy-loaded models into memory now. No-op for any file that
    // isn't on disk. Safe to call from any thread.
    void preloadModels();

    // Drop the in-memory ORT sessions / tokenizer; files on disk stay.
    // Subsequent lazy-access reloads on demand.
    void unloadModels();

    MediaDatabase& db();
    ClapAudioEncoder* audioEncoder() noexcept;
    ClapTextEncoder* textEncoder() noexcept;
    RobertaTokenizer* tokenizer() noexcept;

    // Path helpers. The DB file is fixed at dataDir/MediaDB/media.db; model
    // files live in dataDir/MediaDB/models/ (Phase F's download UI will place
    // them there).
    [[nodiscard]] std::filesystem::path dbPath() const;
    [[nodiscard]] std::filesystem::path modelsDir() const;
    [[nodiscard]] std::filesystem::path audioModelPath() const;
    [[nodiscard]] std::filesystem::path textModelPath() const;
    [[nodiscard]] std::filesystem::path tokenizerJsonPath() const;

    MediaDbContext(const MediaDbContext&) = delete;
    MediaDbContext& operator=(const MediaDbContext&) = delete;

  private:
    MediaDbContext();
    ~MediaDbContext();

    std::unique_ptr<MediaDatabase> db_;
    std::unique_ptr<ClapAudioEncoder> audioEnc_;
    std::unique_ptr<ClapTextEncoder> textEnc_;
    std::unique_ptr<RobertaTokenizer> tokenizer_;
    bool initAttempted_ = false;
    // Counter rather than bool so concurrent loads (audio + text + tokenizer
    // from preloadModels, or a worker triggering text-encoder load while
    // the indexer triggers audio-encoder load) all show as "loading".
    std::atomic<int> loadInProgress_{0};
};

}  // namespace magda::media
