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

    void loadOptionalAi();  // sets audioEnc_/textEnc_/tokenizer_ if files exist

    std::unique_ptr<MediaDatabase> db_;
    std::unique_ptr<ClapAudioEncoder> audioEnc_;
    std::unique_ptr<ClapTextEncoder> textEnc_;
    std::unique_ptr<RobertaTokenizer> tokenizer_;
    bool initAttempted_ = false;
};

}  // namespace magda::media
