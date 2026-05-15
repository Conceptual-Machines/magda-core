#include "MediaDbContext.hpp"

#include <stdexcept>

#include "../core/AppPaths.hpp"
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

    loadOptionalAi();
    return true;
}

void MediaDbContext::loadOptionalAi() {
    // Each piece is optional. A user without the AI Audio Pack still gets
    // filename + metadata + FTS search.
    if (std::filesystem::exists(audioModelPath())) {
        try {
            audioEnc_ = std::make_unique<ClapAudioEncoder>(audioModelPath());
        } catch (const std::exception&) {
            audioEnc_.reset();
        }
    }
    if (std::filesystem::exists(textModelPath())) {
        try {
            textEnc_ = std::make_unique<ClapTextEncoder>(textModelPath());
        } catch (const std::exception&) {
            textEnc_.reset();
        }
    }
    if (std::filesystem::exists(tokenizerJsonPath())) {
        try {
            tokenizer_ = std::make_unique<RobertaTokenizer>(tokenizerJsonPath());
        } catch (const std::exception&) {
            tokenizer_.reset();
        }
    }
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
    return audioEnc_ != nullptr;
}
bool MediaDbContext::hasTextSearch() const noexcept {
    return textEnc_ != nullptr && tokenizer_ != nullptr;
}

MediaDatabase& MediaDbContext::db() {
    if (!db_) {
        throw std::runtime_error("MediaDbContext::db() called before ensureInitialized()");
    }
    return *db_;
}

ClapAudioEncoder* MediaDbContext::audioEncoder() noexcept {
    return audioEnc_.get();
}
ClapTextEncoder* MediaDbContext::textEncoder() noexcept {
    return textEnc_.get();
}
RobertaTokenizer* MediaDbContext::tokenizer() noexcept {
    return tokenizer_.get();
}

}  // namespace magda::media
