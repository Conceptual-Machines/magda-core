#include "command_model_downloader.hpp"

#include <juce_events/juce_events.h>

#include "../daw/core/AppPaths.hpp"
#include "command_model_onnx.hpp"

namespace magda {

namespace {

// The model bundle is pinned to a HuggingFace REVISION, not to per-file
// hashes.
//
// A /resolve/<commit-sha>/ URL is immutable: HF serves that exact blob forever,
// so the revision *is* the content address and separate SHA-256s would be
// redundant bookkeeping. Pinning /resolve/main/ instead — which this used to do
// — means every upload silently invalidates the hashes baked into already
// released builds, and those users get a checksum failure with no way to
// recover.
//
// The pin also makes the app<->model pairing explicit: a build downloads the
// model it was tested against, and publishing a new model cannot change what
// shipped apps fetch. Updating the model is a one-line change here, followed by
// re-running the parity tests against the new bundle.
//
// Sizes stay: they are a cheap existence/completeness check for isInstalled()
// and a progress-bar total, not an integrity mechanism.
struct ManifestEntry {
    const char* filename;
    juce::int64 size;
};

constexpr const char* kRepo = "ConceptualMachines/magda-command-model";
constexpr const char* kRepoUrl = "https://huggingface.co/ConceptualMachines/magda-command-model";

// Bump together with the files, after re-running the parity tests.
constexpr const char* kRevision = "6c311f7ff9543ed0079b27f130ad13d26c1a4d59";

// DeBERTa-v3 base fine-tuned for intent + BIO slot tagging, embedding
// quantized to int8 (the attention/FFN deliberately are NOT — quantizing them
// drops the model from 96.5% to 0.9%; see the POC's findings.md).
constexpr ManifestEntry kFiles[] = {
    {"command_model.onnx", 441544629},
    {"tokenizer.json", 8340513},
    {"maps.json", 1357},
};

juce::String fileUrl(const ManifestEntry& e) {
    return juce::String("https://huggingface.co/") + kRepo + "/resolve/" + kRevision + "/" +
           e.filename;
}

constexpr int kNumFiles = static_cast<int>(sizeof(kFiles) / sizeof(kFiles[0]));

}  // namespace

class CommandModelDownloader::Worker : public juce::Thread {
  public:
    Worker(juce::File destinationDir, ProgressCallback onProgress)
        : juce::Thread("MAGDA CommandModelDownloader"),
          destinationDir_(std::move(destinationDir)),
          onProgress_(std::move(onProgress)) {}

    void run() override {
        Progress p;
        p.bytesTotal = CommandModelDownloader::expectedTotalBytes();
        p.phase = Phase::Downloading;

        if (!destinationDir_.isDirectory()) {
            const auto result = destinationDir_.createDirectory();
            if (result.failed()) {
                p.phase = Phase::Failed;
                p.errorMessage = "Could not create " + destinationDir_.getFullPathName() + ": " +
                                 result.getErrorMessage();
                postProgress(p);
                return;
            }
        }

        for (int i = 0; i < kNumFiles; ++i) {
            p.currentFilename = kFiles[i].filename;
            p.phase = Phase::Downloading;
            postProgress(p);

            if (!downloadOne(kFiles[i], p)) {
                p.phase = threadShouldExit() ? Phase::Cancelled : Phase::Failed;
                postProgress(p);
                return;
            }
        }

        p.phase = Phase::Done;
        p.bytesDone = p.bytesTotal;
        postProgress(p);
    }

  private:
    // Stream into a temp file, verify completeness, then atomically rename.
    bool downloadOne(const ManifestEntry& entry, Progress& p) {
        const auto dest = destinationDir_.getChildFile(entry.filename);

        juce::URL url(fileUrl(entry));
        int statusCode = 0;
        const auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                 .withConnectionTimeoutMs(30000)
                                 .withStatusCode(&statusCode)
                                 .withNumRedirectsToFollow(5);
        auto stream = url.createInputStream(options);
        if (stream == nullptr) {
            p.errorMessage = juce::String("Could not connect to ") + entry.filename;
            return false;
        }
        if (statusCode != 0 && (statusCode < 200 || statusCode >= 300)) {
            p.errorMessage = juce::String(entry.filename) + ": HTTP " + juce::String(statusCode);
            return false;
        }

        juce::TemporaryFile tmp(dest);
        auto out = std::make_unique<juce::FileOutputStream>(tmp.getFile());
        if (!out->openedOk()) {
            p.errorMessage = juce::String("Could not write to ") + tmp.getFile().getFullPathName();
            return false;
        }

        constexpr int kChunk = 1 << 16;  // 64 KiB
        juce::MemoryBlock buf(kChunk);
        for (;;) {
            if (threadShouldExit())
                return false;  // tmp destructs without renaming
            const int read = stream->read(buf.getData(), kChunk);
            if (read < 0) {
                p.errorMessage = juce::String("Read error on ") + entry.filename;
                return false;
            }
            if (read == 0)
                break;
            if (!out->write(buf.getData(), static_cast<size_t>(read))) {
                p.errorMessage = juce::String("Write error on ") + dest.getFullPathName();
                return false;
            }
            p.bytesDone += read;
            postProgress(p);
        }
        out->flush();
        if (out->getStatus().failed()) {
            p.errorMessage = juce::String("Flush failed on ") + dest.getFullPathName();
            return false;
        }

#if JUCE_WINDOWS
        // Release the handle before the rename: JUCE opens with
        // FILE_SHARE_READ only, so a held handle is a sharing violation.
        out.reset();
#endif

        p.phase = Phase::Verifying;
        postProgress(p);

        // Size is the completeness check — a truncated download is the failure
        // that actually happens. Integrity comes from the pinned revision:
        // /resolve/<sha>/ cannot serve different bytes later.
        if (tmp.getFile().getSize() != entry.size) {
            p.errorMessage = juce::String(entry.filename) + ": expected " +
                             juce::String(entry.size) + " bytes, got " +
                             juce::String(tmp.getFile().getSize());
            return false;
        }

        if (!tmp.overwriteTargetFileWithTemporary()) {
            p.errorMessage = juce::String("Could not move ") + entry.filename + " into place";
            return false;
        }
        return true;
    }

    void postProgress(Progress p) {
        if (!onProgress_)
            return;
        auto cb = onProgress_;
        juce::MessageManager::callAsync([cb, p]() { cb(p); });
    }

    juce::File destinationDir_;
    ProgressCallback onProgress_;
};

// ===========================================================================

CommandModelDownloader::CommandModelDownloader() = default;

CommandModelDownloader::~CommandModelDownloader() {
    cancel();
    if (worker_)
        worker_->stopThread(10000);
}

juce::File CommandModelDownloader::modelsDir() {
    // Single source of truth: the same directory the backend loads from.
    return juce::File(juce::String(CommandModelOnnx::defaultAssetDir().string()));
}

std::vector<juce::File> CommandModelDownloader::modelFiles() {
    std::vector<juce::File> files;
    files.reserve(static_cast<size_t>(kNumFiles));
    for (int i = 0; i < kNumFiles; ++i)
        files.push_back(modelsDir().getChildFile(kFiles[i].filename));
    return files;
}

const char* CommandModelDownloader::displayName() {
    return "Command model (DeBERTa-v3)";
}

const char* CommandModelDownloader::sourceUrl() {
    return kRepoUrl;
}

bool CommandModelDownloader::isInstalled() {
    for (int i = 0; i < kNumFiles; ++i) {
        auto f = modelsDir().getChildFile(kFiles[i].filename);
        if (!f.existsAsFile() || f.getSize() != kFiles[i].size)
            return false;
    }
    return true;
}

juce::int64 CommandModelDownloader::expectedTotalBytes() {
    juce::int64 total = 0;
    for (int i = 0; i < kNumFiles; ++i)
        total += kFiles[i].size;
    return total;
}

bool CommandModelDownloader::remove() {
    bool ok = true;
    for (const auto& f : modelFiles())
        if (f.existsAsFile() && !f.deleteFile())
            ok = false;
    return ok;
}

void CommandModelDownloader::start(ProgressCallback onProgress) {
    if (worker_ && worker_->isThreadRunning())
        return;
    // Keep one destination for the whole transfer even if the setting changes
    // while the background worker is running.
    worker_ = std::make_unique<Worker>(modelsDir(), std::move(onProgress));
    worker_->startThread();
}

void CommandModelDownloader::cancel() {
    if (worker_)
        worker_->signalThreadShouldExit();
}

bool CommandModelDownloader::isRunning() const noexcept {
    return worker_ != nullptr && worker_->isThreadRunning();
}

}  // namespace magda
