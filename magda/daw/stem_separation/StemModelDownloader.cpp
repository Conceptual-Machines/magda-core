// See StemModelDownloader.hpp.

#include "StemModelDownloader.hpp"

#include <juce_cryptography/juce_cryptography.h>
#include <juce_events/juce_events.h>

#include "AppPaths.hpp"

namespace magda::stems {

namespace {

// ---- Manifest -----------------------------------------------------------
//
// Sizes and SHA-256s come from the HuggingFace API (LFS oid) for the pinned
// upstream revision. If upstream re-exports, update size + hash together.

struct ManifestEntry {
    const char* filename;
    const char* url;
    const char* sha256;
    juce::int64 size;
    const char* displayName;
};

// StemSplitio/htdemucs-onnx revision d54ed9eb, fp32 single-file export of
// Meta's htdemucs (STFT/iSTFT in-graph, so the host contract is waveform
// in, stems out).
constexpr ManifestEntry kHtdemucs = {
    "htdemucs.onnx",
    "https://huggingface.co/StemSplitio/htdemucs-onnx/resolve/main/htdemucs.onnx",
    "68d0bf16428ef66e692cdff8a9ccf28f1ef3f69440d57e58605a4cc55fcc5e74",
    316446953,
    "Demucs (4-stem)",
};

const ManifestEntry& manifestFor(StemModel model) {
    switch (model) {
        case StemModel::Htdemucs:
            break;
    }
    return kHtdemucs;
}

}  // namespace

// ===========================================================================
// Worker — background thread that runs the actual download and verification
// ===========================================================================

class StemModelDownloader::Worker : public juce::Thread {
  public:
    Worker(StemModel model, ProgressCallback onProgress)
        : juce::Thread("MAGDA StemModelDownloader"),
          model_(model),
          onProgress_(std::move(onProgress)) {}

    void run() override {
        const auto& entry = manifestFor(model_);

        Progress p;
        p.currentFilename = entry.filename;
        p.bytesTotal = entry.size;
        p.phase = Phase::Downloading;
        postProgress(p);

        auto destDir = StemModelDownloader::modelsDir();
        if (!destDir.exists())
            destDir.createDirectory();

        if (!downloadOne(entry, p)) {
            p.phase = threadShouldExit() ? Phase::Cancelled : Phase::Failed;
            postProgress(p);
            return;
        }

        p.phase = Phase::Done;
        p.bytesDone = p.bytesTotal;
        postProgress(p);
    }

  private:
    // Stream `entry.url` into place via a temp file, verifying the SHA-256
    // before the atomic rename; mirrors SampleTaggerDownloader.
    bool downloadOne(const ManifestEntry& entry, Progress& p) {
        const auto dest = StemModelDownloader::modelFile(model_);

        juce::URL url(entry.url);
        int statusCode = 0;
        const auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                 .withConnectionTimeoutMs(30000)
                                 .withStatusCode(&statusCode);
        auto stream = url.createInputStream(options);
        if (stream == nullptr) {
            p.errorMessage = juce::String("Could not connect to ") + entry.filename;
            return false;
        }
        if (statusCode != 0 && (statusCode < 200 || statusCode >= 300)) {
            p.errorMessage = juce::String(entry.filename) + ": HTTP " + juce::String(statusCode);
            return false;
        }

        juce::TemporaryFile tmp(dest);  // atomic rename on commit
        auto out = std::make_unique<juce::FileOutputStream>(tmp.getFile());
        if (!out->openedOk()) {
            p.errorMessage = juce::String("Could not write to ") + tmp.getFile().getFullPathName();
            return false;
        }

        constexpr int kChunk = 1 << 16;  // 64 KiB
        juce::MemoryBlock buf(kChunk);
        for (;;) {
            if (threadShouldExit())
                return false;  // tmp file destructs without renaming
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
        // Release the temp file handle before the rename below; JUCE opens
        // with FILE_SHARE_READ only on Windows, so a held handle would make
        // overwriteTargetFileWithTemporary() fail with a sharing violation.
        out.reset();
#endif

        p.phase = Phase::Verifying;
        postProgress(p);

        const auto actualHash = hashFile(tmp.getFile());
        if (!actualHash.equalsIgnoreCase(entry.sha256)) {
            p.errorMessage = juce::String("Checksum mismatch on ") + entry.filename;
            return false;
        }

        if (!tmp.overwriteTargetFileWithTemporary()) {
            p.errorMessage = juce::String("Could not move ") + entry.filename + " into place";
            return false;
        }
        return true;
    }

    static juce::String hashFile(const juce::File& f) {
        juce::FileInputStream in(f);
        if (!in.openedOk())
            return {};
        juce::SHA256 hash(in);
        return hash.toHexString();
    }

    void postProgress(Progress p) {
        if (!onProgress_)
            return;
        auto cb = onProgress_;
        juce::MessageManager::callAsync([cb, p]() { cb(p); });
    }

    StemModel model_;
    ProgressCallback onProgress_;
};

// ===========================================================================
// StemModelDownloader
// ===========================================================================

StemModelDownloader::StemModelDownloader(StemModel model) : model_(model) {}

StemModelDownloader::~StemModelDownloader() {
    cancel();
    if (worker_)
        worker_->stopThread(10000);  // up to 10 s for an in-flight chunk
}

juce::File StemModelDownloader::modelsDir() {
    return paths::dataDir().getChildFile("StemSeparation").getChildFile("models");
}

juce::File StemModelDownloader::modelFile(StemModel model) {
    return modelsDir().getChildFile(manifestFor(model).filename);
}

const char* StemModelDownloader::displayName(StemModel model) {
    return manifestFor(model).displayName;
}

bool StemModelDownloader::isInstalled(StemModel model) {
    const auto& entry = manifestFor(model);
    auto f = modelFile(model);
    return f.existsAsFile() && f.getSize() == entry.size;
}

juce::int64 StemModelDownloader::expectedTotalBytes(StemModel model) {
    return manifestFor(model).size;
}

bool StemModelDownloader::remove(StemModel model) {
    auto f = modelFile(model);
    return !f.existsAsFile() || f.deleteFile();
}

void StemModelDownloader::start(ProgressCallback onProgress) {
    if (worker_ && worker_->isThreadRunning())
        return;
    worker_ = std::make_unique<Worker>(model_, std::move(onProgress));
    worker_->startThread();
}

void StemModelDownloader::cancel() {
    if (worker_)
        worker_->signalThreadShouldExit();
}

bool StemModelDownloader::isRunning() const noexcept {
    return worker_ != nullptr && worker_->isThreadRunning();
}

}  // namespace magda::stems
