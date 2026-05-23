#include "SampleTaggerDownloader.hpp"

#include <juce_cryptography/juce_cryptography.h>
#include <juce_events/juce_events.h>

#include <array>
#include <filesystem>
#include <system_error>

#include "MediaDbContext.hpp"

namespace magda::media {

namespace {

// ---- Manifest -----------------------------------------------------------
//
// URLs, sizes, and SHA-256s are baked into the binary. SHA-256s come from
// scripts/upload_sample_tagger_to_hf.py's model-card output. If we re-export
// the ONNX, re-run that script's --dry-run and update both numbers here AND
// the model card in lockstep.

struct ManifestEntry {
    const char* filename;
    const char* url;
    const char* sha256;
    juce::int64 size;
};

constexpr std::array<ManifestEntry, 3> kManifest = {{
    {
        "clap_audio.onnx",
        "https://huggingface.co/ConceptualMachines/magda-sample-tagger/resolve/main/"
        "clap_audio.onnx",
        "3f42f71e555b62709910b6efa66fa5879f00d9571874b12b0fa674f82dbfe332",
        117275256,
    },
    {
        "clap_text.onnx",
        "https://huggingface.co/ConceptualMachines/magda-sample-tagger/resolve/main/clap_text.onnx",
        "c07b27204836877d5b615c103685b66ea8f21bc6b5b70a572be356125423a8bf",
        501425065,
    },
    {
        "tokenizer.json",
        "https://huggingface.co/ConceptualMachines/magda-sample-tagger/resolve/main/tokenizer.json",
        "4fd1d86b4f5b53f40a609fcd11c1f34024b735f870a07439d70202b98493661a",
        3558802,
    },
}};

juce::File modelsDirAsFile() {
    return juce::File(juce::String(MediaDbContext::getInstance().modelsDir().string()));
}

juce::File destinationFile(const ManifestEntry& entry) {
    return modelsDirAsFile().getChildFile(entry.filename);
}

}  // namespace

// ===========================================================================
// Worker — background thread that runs the actual download and verification
// ===========================================================================

class SampleTaggerDownloader::Worker : public juce::Thread {
  public:
    Worker(SampleTaggerDownloader& owner, ProgressCallback onProgress)
        : juce::Thread("MAGDA SampleTaggerDownloader"),
          owner_(owner),
          onProgress_(std::move(onProgress)) {}

    void run() override {
        Progress p;
        p.totalFiles = static_cast<int>(kManifest.size());
        p.totalBytesAll = SampleTaggerDownloader::expectedTotalBytes();
        p.phase = Phase::Downloading;

        const auto destDir = modelsDirAsFile();
        if (!destDir.exists()) {
            std::error_code ec;
            std::filesystem::create_directories(destDir.getFullPathName().toStdString(), ec);
            // Ignore ec — if the directory can't be created we'll fail on
            // the file write below with a more specific error.
        }

        juce::int64 cumulativeBytes = 0;

        for (int i = 0; i < static_cast<int>(kManifest.size()); ++i) {
            const auto& entry = kManifest[i];

            p.currentFileIndex = i;
            p.currentFilename = entry.filename;
            p.bytesDoneInFile = 0;
            p.totalBytesInFile = entry.size;
            p.bytesDoneAll = cumulativeBytes;
            p.phase = Phase::Downloading;
            postProgress(p);

            if (!downloadOne(entry, p, cumulativeBytes)) {
                if (threadShouldExit()) {
                    p.phase = Phase::Cancelled;
                    postProgress(p);
                } else {
                    // errorMessage already populated by downloadOne.
                    p.phase = Phase::Failed;
                    postProgress(p);
                }
                return;
            }

            cumulativeBytes += entry.size;
        }

        p.phase = Phase::Done;
        p.bytesDoneAll = p.totalBytesAll;
        postProgress(p);
    }

  private:
    // Stream `entry.url` into `<modelsDir>/<filename>`, updating `p` and
    // firing `onProgress_` every ~64 KiB. Returns true on success, false
    // on cancel or any error (errorMessage populated). On any failure the
    // partially-written file is removed.
    bool downloadOne(const ManifestEntry& entry, Progress& p, juce::int64 cumulativeBytes) {
        const auto dest = destinationFile(entry);

        // Open the URL. Long connection timeout: the macOS / Windows
        // installers download these models on fresh setups over varied
        // network speeds. Cancellation is honoured between read chunks
        // so we don't need the URL stack itself to be interruptible.
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
            if (threadShouldExit()) {
                return false;  // tmp file destructs without renaming
            }
            const int read = stream->read(buf.getData(), kChunk);
            if (read < 0) {
                p.errorMessage = juce::String("Read error on ") + entry.filename;
                return false;
            }
            if (read == 0) {
                break;
            }
            if (!out->write(buf.getData(), static_cast<size_t>(read))) {
                p.errorMessage = juce::String("Write error on ") + dest.getFullPathName();
                return false;
            }
            p.bytesDoneInFile += read;
            p.bytesDoneAll = cumulativeBytes + p.bytesDoneInFile;
            postProgress(p);
        }
        out->flush();
        if (out->getStatus().failed()) {
            p.errorMessage = juce::String("Flush failed on ") + dest.getFullPathName();
            return false;
        }

#if JUCE_WINDOWS
        // Release the temp file handle before the rename below. JUCE
        // FileOutputStream opens with FILE_SHARE_READ only on Windows, so
        // the temp file cannot be moved or deleted while we still hold it
        // — overwriteTargetFileWithTemporary() would otherwise fail every
        // retry with a sharing violation. On macOS/Linux open files can be
        // renamed normally, so we leave the destructor to run at scope end.
        out.reset();
#endif

        // Verify before swapping the temp file into its final name. A
        // tampered or partial download must not appear "installed" to the
        // lazy-load code in MediaDbContext.
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
        if (!in.openedOk()) {
            return {};
        }
        juce::SHA256 hash(in);
        return hash.toHexString();
    }

    void postProgress(Progress p) {
        if (!onProgress_) {
            return;
        }
        auto cb = onProgress_;
        juce::MessageManager::callAsync([cb, p]() { cb(p); });
    }

    SampleTaggerDownloader& owner_;
    ProgressCallback onProgress_;
};

// ===========================================================================
// SampleTaggerDownloader
// ===========================================================================

SampleTaggerDownloader::SampleTaggerDownloader() = default;

SampleTaggerDownloader::~SampleTaggerDownloader() {
    cancel();
    if (worker_) {
        worker_->stopThread(10000);  // up to 10 s for an in-flight chunk to finish
    }
}

bool SampleTaggerDownloader::isInstalled() {
    auto dir = modelsDirAsFile();
    for (const auto& entry : kManifest) {
        auto f = dir.getChildFile(entry.filename);
        if (!f.existsAsFile()) {
            return false;
        }
        if (f.getSize() != entry.size) {
            return false;  // size mismatch implies partial / corrupt
        }
    }
    return true;
}

juce::int64 SampleTaggerDownloader::expectedTotalBytes() {
    juce::int64 total = 0;
    for (const auto& entry : kManifest) {
        total += entry.size;
    }
    return total;
}

void SampleTaggerDownloader::start(ProgressCallback onProgress) {
    if (worker_ && worker_->isThreadRunning()) {
        return;
    }
    worker_ = std::make_unique<Worker>(*this, std::move(onProgress));
    worker_->startThread();
}

void SampleTaggerDownloader::cancel() {
    if (worker_) {
        worker_->signalThreadShouldExit();
    }
}

bool SampleTaggerDownloader::isRunning() const noexcept {
    return worker_ != nullptr && worker_->isThreadRunning();
}

}  // namespace magda::media
