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
    return std::filesystem::path(
               magda::paths::dataDir().getChildFile("MediaDB").getFullPathName().toStdString()) /
           "media.db";
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

void MediaDbContext::loadOptionalAi() {
    // Retained for completeness but no longer called from ensureInitialized().
    // Useful if a future "preload models" toggle in preferences wants to
    // warm everything up at startup.
    (void)audioEncoder();
    (void)textEncoder();
    (void)tokenizer();
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
    try {
        tokenizer_ = std::make_unique<RobertaTokenizer>(tokenizerJsonPath());
    } catch (const std::exception&) {
        tokenizer_.reset();
    }
    return tokenizer_.get();
}

}  // namespace magda::media
