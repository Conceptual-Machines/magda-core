// See StemModelDownloader.hpp.

#include "StemModelDownloader.hpp"

#include <juce_cryptography/juce_cryptography.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

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
};

struct ModelManifest {
    const char* displayName;
    const char* sourceUrl;  // human-facing HuggingFace repo page
    const ManifestEntry* files;
    int numFiles;
};

// StemSplitio/htdemucs-onnx revision d54ed9eb, fp32 single-file export of
// Meta's htdemucs (STFT/iSTFT in-graph, so the host contract is waveform
// in, stems out).
constexpr ManifestEntry kHtdemucsFiles[] = {
    {
        "htdemucs.onnx",
        "https://huggingface.co/StemSplitio/htdemucs-onnx/resolve/main/htdemucs.onnx",
        "68d0bf16428ef66e692cdff8a9ccf28f1ef3f69440d57e58605a4cc55fcc5e74",
        316446953,
    },
};

// csukuangfj/sherpa-onnx-spleeter-2stems fp32 conversion of Deezer's
// Spleeter 2-stem release: one UNet per stem over a host-side STFT.
constexpr ManifestEntry kSpleeter2sFiles[] = {
    {
        "vocals.onnx",
        "https://huggingface.co/csukuangfj/sherpa-onnx-spleeter-2stems/resolve/main/vocals.onnx",
        "bdc16ab6bf6117ddd4842c19e80e40e2be188fc555295064d424616b0224ac97",
        39318336,
    },
    {
        "accompaniment.onnx",
        "https://huggingface.co/csukuangfj/sherpa-onnx-spleeter-2stems/resolve/main/"
        "accompaniment.onnx",
        "671ace17acd3720674a2bc14de32ac6292453dec20d9eb0ba4255d4ad8e3d8c0",
        39318343,
    },
};

constexpr ModelManifest kHtdemucs = {
    "Demucs (4-stem)", "https://huggingface.co/StemSplitio/htdemucs-onnx", kHtdemucsFiles, 1};
constexpr ModelManifest kSpleeter2s = {
    "Spleeter (2-stem)", "https://huggingface.co/csukuangfj/sherpa-onnx-spleeter-2stems",
    kSpleeter2sFiles, 2};

const ModelManifest& manifestFor(StemModel model) {
    switch (model) {
        case StemModel::Htdemucs:
            return kHtdemucs;
        case StemModel::Spleeter2s:
            return kSpleeter2s;
    }
    return kHtdemucs;
}

}  // namespace

class ReadDeadline {
  public:
    explicit ReadDeadline(juce::WebInputStream& stream) : stream_(stream) {
        watchdog_ = std::thread([this] {
            std::unique_lock lock(mutex_);
            auto observedGeneration = generation_;
            while (!stopping_) {
                if (condition_.wait_for(lock, std::chrono::seconds(30), [&] {
                        return stopping_ || generation_ != observedGeneration;
                    })) {
                    observedGeneration = generation_;
                    continue;
                }
                timedOut_.store(true);
                lock.unlock();
                stream_.cancel();
                return;
            }
        });
    }

    ~ReadDeadline() {
        {
            std::scoped_lock lock(mutex_);
            stopping_ = true;
        }
        condition_.notify_one();
        if (watchdog_.joinable())
            watchdog_.join();
    }

    void reset() {
        {
            std::scoped_lock lock(mutex_);
            ++generation_;
        }
        condition_.notify_one();
    }

    [[nodiscard]] bool timedOut() const {
        return timedOut_.load();
    }

  private:
    juce::WebInputStream& stream_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread watchdog_;
    std::size_t generation_ = 0;
    bool stopping_ = false;
    std::atomic<bool> timedOut_{false};
};

// ===========================================================================
// Worker — background thread that runs the actual download and verification
// ===========================================================================

class StemModelDownloader::Worker : public juce::Thread {
  public:
    Worker(StemModel model, ProgressCallback onProgress)
        : juce::Thread("MAGDA StemModelDownloader"),
          model_(model),
          onProgress_(std::move(onProgress)) {}

    void cancelDownload() {
        signalThreadShouldExit();
        if (auto* stream = activeStream_.load())
            stream->cancel();
    }

    void run() override {
        const auto& manifest = manifestFor(model_);

        Progress p;
        p.bytesTotal = StemModelDownloader::expectedTotalBytes(model_);
        p.phase = Phase::Downloading;

        auto destDir = StemModelDownloader::modelsDir();
        if (!destDir.exists())
            destDir.createDirectory();

        for (int i = 0; i < manifest.numFiles; ++i) {
            const auto& entry = manifest.files[i];
            p.currentFilename = entry.filename;
            p.phase = Phase::Downloading;
            postProgress(p);

            if (!downloadOne(entry, p)) {
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
    // Stream `entry.url` into place via a temp file, verifying the SHA-256
    // before the atomic rename; mirrors SampleTaggerDownloader.
    bool downloadOne(const ManifestEntry& entry, Progress& p) {
        const auto dest = StemModelDownloader::modelsDir().getChildFile(entry.filename);

        juce::WebInputStream stream(juce::URL(entry.url), false);
        stream.withConnectionTimeout(30000);
        activeStream_.store(&stream);
        struct ClearActiveStream {
            std::atomic<juce::WebInputStream*>& active;
            ~ClearActiveStream() {
                active.store(nullptr);
            }
        } clearActiveStream{activeStream_};

        if (!stream.connect(nullptr)) {
            p.errorMessage = juce::String("Could not connect to ") + entry.filename;
            return false;
        }
        const int statusCode = stream.getStatusCode();
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
        ReadDeadline readDeadline(stream);
        juce::int64 fileBytes = 0;
        int lastPostedPercent = -1;
        for (;;) {
            if (threadShouldExit())
                return false;  // tmp file destructs without renaming
            const int read = stream.read(buf.getData(), kChunk);
            readDeadline.reset();
            if (readDeadline.timedOut()) {
                p.errorMessage = juce::String("Read timed out on ") + entry.filename;
                return false;
            }
            if (threadShouldExit())
                return false;
            if (read < 0) {
                p.errorMessage = juce::String("Read error on ") + entry.filename;
                return false;
            }
            if (read == 0)
                break;
            if (fileBytes > entry.size - read) {
                p.errorMessage =
                    juce::String("Download exceeded expected size for ") + entry.filename;
                return false;
            }
            if (!out->write(buf.getData(), static_cast<size_t>(read))) {
                p.errorMessage = juce::String("Write error on ") + dest.getFullPathName();
                return false;
            }
            fileBytes += read;
            p.bytesDone += read;
            const int percent =
                p.bytesTotal > 0 ? static_cast<int>((100 * p.bytesDone) / p.bytesTotal) : 0;
            if (percent != lastPostedPercent) {
                lastPostedPercent = percent;
                postProgress(p);
            }
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
    std::atomic<juce::WebInputStream*> activeStream_{nullptr};
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

std::vector<juce::File> StemModelDownloader::modelFiles(StemModel model) {
    const auto& manifest = manifestFor(model);
    std::vector<juce::File> files;
    files.reserve(static_cast<size_t>(manifest.numFiles));
    for (int i = 0; i < manifest.numFiles; ++i)
        files.push_back(modelsDir().getChildFile(manifest.files[i].filename));
    return files;
}

const char* StemModelDownloader::displayName(StemModel model) {
    return manifestFor(model).displayName;
}

const char* StemModelDownloader::sourceUrl(StemModel model) {
    return manifestFor(model).sourceUrl;
}

bool StemModelDownloader::isInstalled(StemModel model) {
    const auto& manifest = manifestFor(model);
    for (int i = 0; i < manifest.numFiles; ++i) {
        auto f = modelsDir().getChildFile(manifest.files[i].filename);
        if (!f.existsAsFile() || f.getSize() != manifest.files[i].size)
            return false;
    }
    return true;
}

juce::int64 StemModelDownloader::expectedTotalBytes(StemModel model) {
    const auto& manifest = manifestFor(model);
    juce::int64 total = 0;
    for (int i = 0; i < manifest.numFiles; ++i)
        total += manifest.files[i].size;
    return total;
}

bool StemModelDownloader::remove(StemModel model) {
    bool ok = true;
    for (const auto& f : modelFiles(model))
        if (f.existsAsFile() && !f.deleteFile())
            ok = false;
    return ok;
}

void StemModelDownloader::start(ProgressCallback onProgress) {
    if (worker_ && worker_->isThreadRunning())
        return;
    worker_ = std::make_unique<Worker>(model_, std::move(onProgress));
    worker_->startThread();
}

void StemModelDownloader::cancel() {
    if (worker_)
        worker_->cancelDownload();
}

bool StemModelDownloader::isRunning() const noexcept {
    return worker_ != nullptr && worker_->isThreadRunning();
}

}  // namespace magda::stems
