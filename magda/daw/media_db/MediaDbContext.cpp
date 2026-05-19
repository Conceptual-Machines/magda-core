#include "MediaDbContext.hpp"

#include <stdexcept>

#include "../core/AppPaths.hpp"
#include "../core/Config.hpp"
#include "ClapAudioEncoder.hpp"
#include "ClapTextEncoder.hpp"
#include "MediaDatabase.hpp"
#include "RobertaTokenizer.hpp"

namespace magda::media {

namespace {
constexpr const char* kAudioModelFilename = "clap_audio.onnx";
constexpr const char* kTextModelFilename = "clap_text.onnx";
constexpr const char* kTokenizerFilename = "tokenizer.json";
}  // namespace

MediaDbContext& MediaDbContext::getInstance() {
    static MediaDbContext instance;
    return instance;
}

MediaDbContext::MediaDbContext() = default;
MediaDbContext::~MediaDbContext() = default;

std::filesystem::path MediaDbContext::dbPath() const {
    // User override: same fall-back semantics as modelsDir(). Lets users
    // park the (potentially large) index on a different drive without
    // symlinking. If the override directory has gone missing (drive
    // unplugged), fall back so we don't crash on launch with a dead path.
    const auto override = magda::Config::getInstance().getMediaDbDir();
    if (!override.empty()) {
        std::filesystem::path p(override);
        std::error_code ec;
        if (std::filesystem::is_directory(p, ec)) {
            return p / "media.db";
        }
    }
    // Default: MediaDB/db/media.db, sibling of MediaDB/models/ so the
    // two resources read as parallel.
    return std::filesystem::path(
               magda::paths::dataDir().getChildFile("MediaDB").getFullPathName().toStdString()) /
           "db" / "media.db";
}

std::filesystem::path MediaDbContext::modelsDir() const {
    // User override: if Config has a non-empty path AND it points at a
    // real directory, use it. Lets the user keep the ~600 MB Sample
    // Tagger bundle on an external drive. Falls back to the default
    // when unset or when the override directory has gone missing
    // (drive unplugged, etc.) — fallback prevents the downloader and
    // lazy-load code from chasing dead paths.
    const auto override = magda::Config::getInstance().getSampleTaggerModelsDir();
    if (!override.empty()) {
        std::filesystem::path p(override);
        std::error_code ec;
        if (std::filesystem::is_directory(p, ec)) {
            return p;
        }
    }
    // Default: MediaDB/models/ — sibling of MediaDB/db/ under the
    // shared MediaDB/ parent so the two defaults read as parallel
    // resources rather than the model living inside the DB folder.
    return std::filesystem::path(
               magda::paths::dataDir().getChildFile("MediaDB").getFullPathName().toStdString()) /
           "models";
}

std::filesystem::path MediaDbContext::audioModelPath() const {
    return modelsDir() / kAudioModelFilename;
}
std::filesystem::path MediaDbContext::textModelPath() const {
    return modelsDir() / kTextModelFilename;
}
std::filesystem::path MediaDbContext::tokenizerJsonPath() const {
    return modelsDir() / kTokenizerFilename;
}

bool MediaDbContext::ensureInitialized() {
    if (db_) {
        return true;
    }
    initAttempted_ = true;

    // Make sure the parent dirs exist before SQLite tries to touch them.
    const auto parent = dbPath().parent_path();
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    std::filesystem::create_directories(modelsDir(), ec);

    try {
        db_ = std::make_unique<MediaDatabase>(dbPath());
    } catch (const std::exception&) {
        db_.reset();
        return false;
    }
    // Encoders / tokenizer are loaded lazily — the audio one only when
    // indexing starts, the text one + tokenizer only when a text query
    // runs. See audioEncoder() / textEncoder() / tokenizer() below.
    return true;
}

void MediaDbContext::preloadModels() {
    // Force the lazy accessors to instantiate now. The "Load on startup"
    // Config toggle and the AI Settings → Sample Tagger → Load button both
    // call this; running it on a background thread keeps the UI fluid
    // (this method itself blocks until each ORT Session is built).
    (void)audioEncoder();
    (void)textEncoder();
    (void)tokenizer();
}

void MediaDbContext::unloadModels() {
    audioEnc_.reset();
    textEnc_.reset();
    tokenizer_.reset();
}

bool MediaDbContext::wipeAll() {
    if (!ensureInitialized()) {
        return false;
    }
    // Single transaction so the user can't end up with a half-deleted
    // DB after a power loss. media_file has ON DELETE CASCADE for
    // media_tag / media_embedding / media_metadata, but the FTS5
    // contentless table doesn't follow FKs so it gets an explicit
    // DELETE. PRAGMA foreign_keys is set to ON in Schema.hpp.
    try {
        db_->execute("BEGIN");
        db_->execute("DELETE FROM media_file");
        db_->execute("DELETE FROM media_fts");
        db_->execute("COMMIT");
    } catch (const std::exception&) {
        try {
            db_->execute("ROLLBACK");
        } catch (...) {
            // best-effort cleanup; nothing to do if rollback also fails
        }
        return false;
    }
    return true;
}

void MediaDbContext::resetForReopen() {
    audioEnc_.reset();
    textEnc_.reset();
    tokenizer_.reset();
    db_.reset();
    initAttempted_ = false;
}

void MediaDbContext::shutdown() {
    audioEnc_.reset();
    textEnc_.reset();
    tokenizer_.reset();
    db_.reset();
    initAttempted_ = false;
}

bool MediaDbContext::isReady() const noexcept {
    return db_ != nullptr;
}
bool MediaDbContext::hasAudioEncoder() const noexcept {
    // "Has" means "model file is present on disk" — whether or not it's
    // been loaded yet. The lazy load happens on first audioEncoder() call.
    return std::filesystem::exists(audioModelPath());
}
bool MediaDbContext::hasTextSearch() const noexcept {
    return std::filesystem::exists(textModelPath()) && std::filesystem::exists(tokenizerJsonPath());
}

bool MediaDbContext::isAudioEncoderLoaded() const noexcept {
    return audioEnc_ != nullptr;
}
bool MediaDbContext::isTextEncoderLoaded() const noexcept {
    return textEnc_ != nullptr;
}
bool MediaDbContext::isTokenizerLoaded() const noexcept {
    return tokenizer_ != nullptr;
}
bool MediaDbContext::isLoadInProgress() const noexcept {
    return loadInProgress_.load() > 0;
}

// RAII guard around the loadInProgress_ counter. Increment on entry,
// decrement on scope exit so the status indicator can show "loading"
// while any of the three ORT/tokenizer constructions are running.
namespace {
struct LoadGuard {
    std::atomic<int>& counter;
    explicit LoadGuard(std::atomic<int>& c) : counter(c) {
        counter.fetch_add(1, std::memory_order_relaxed);
    }
    ~LoadGuard() {
        counter.fetch_sub(1, std::memory_order_relaxed);
    }
};
}  // namespace

MediaDatabase& MediaDbContext::db() {
    if (!db_) {
        throw std::runtime_error("MediaDbContext::db() called before ensureInitialized()");
    }
    return *db_;
}

// The next three accessors are lazy: they bring the model into memory the
// first time someone asks. ORT session construction reads the .onnx file
// (~100 MB for audio, ~480 MB for text) plus mmaps/allocates working
// buffers, so deferring keeps app launch cheap and lets MAGDA hold zero
// model state when the user only browses by filename / filters.
ClapAudioEncoder* MediaDbContext::audioEncoder() noexcept {
    if (audioEnc_) {
        return audioEnc_.get();
    }
    if (!std::filesystem::exists(audioModelPath())) {
        return nullptr;
    }
    LoadGuard guard(loadInProgress_);
    try {
        audioEnc_ = std::make_unique<ClapAudioEncoder>(audioModelPath());
    } catch (const std::exception&) {
        audioEnc_.reset();
    }
    return audioEnc_.get();
}

ClapTextEncoder* MediaDbContext::textEncoder() noexcept {
    if (textEnc_) {
        return textEnc_.get();
    }
    if (!std::filesystem::exists(textModelPath())) {
        return nullptr;
    }
    LoadGuard guard(loadInProgress_);
    try {
        textEnc_ = std::make_unique<ClapTextEncoder>(textModelPath());
    } catch (const std::exception&) {
        textEnc_.reset();
    }
    return textEnc_.get();
}

RobertaTokenizer* MediaDbContext::tokenizer() noexcept {
    if (tokenizer_) {
        return tokenizer_.get();
    }
    if (!std::filesystem::exists(tokenizerJsonPath())) {
        return nullptr;
    }
    LoadGuard guard(loadInProgress_);
    try {
        tokenizer_ = std::make_unique<RobertaTokenizer>(tokenizerJsonPath());
    } catch (const std::exception&) {
        tokenizer_.reset();
    }
    return tokenizer_.get();
}

}  // namespace magda::media
