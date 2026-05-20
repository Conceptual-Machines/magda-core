// Downloads the MAGDA Sample Tagger ONNX bundle from HuggingFace into the
// app's data dir (issue #768).
//
// The bundle is three files (~595 MB total): the CLAP audio encoder, the
// CLAP text encoder, and the RoBERTa tokenizer. Without them MAGDA's media
// DB falls back to FTS-only filename/tag search. With them installed, the
// existing lazy-load paths in MediaDbContext::audioEncoder() /
// textEncoder() / tokenizer() pick them up on next access — no restart
// required.
//
// Hosted at ConceptualMachines/magda-sample-tagger on HuggingFace; URLs
// and expected SHA-256s are baked into the implementation's manifest so a
// caller-side tampering check is automatic.
//
// Threading: start() spawns a background juce::Thread. Progress and
// completion callbacks fire on the MAGDA message thread via callAsync.

#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <memory>

namespace magda::media {

class SampleTaggerDownloader {
  public:
    enum class Phase {
        Idle,
        Downloading,
        Verifying,
        Done,
        Failed,
        Cancelled,
    };

    struct Progress {
        Phase phase = Phase::Idle;
        int currentFileIndex = 0;  // 0-based index into the 3-file manifest
        int totalFiles = 0;
        juce::String currentFilename;  // e.g. "clap_audio.onnx"
        juce::int64 bytesDoneInFile = 0;
        juce::int64 totalBytesInFile = 0;
        juce::int64 bytesDoneAll = 0;
        juce::int64 totalBytesAll = 0;
        juce::String errorMessage;  // populated when phase == Failed
    };

    using ProgressCallback = std::function<void(const Progress&)>;

    SampleTaggerDownloader();
    ~SampleTaggerDownloader();

    SampleTaggerDownloader(const SampleTaggerDownloader&) = delete;
    SampleTaggerDownloader& operator=(const SampleTaggerDownloader&) = delete;

    // Quick existence check — every manifest file is present in
    // MediaDbContext::modelsDir() with the expected size. Skips the
    // hash check (expensive on a 500 MB file) so callers can use this
    // every time the DB browser repaints without burning CPU.
    [[nodiscard]] static bool isInstalled();

    // Total bytes the bundle will occupy on disk once downloaded. Useful
    // for sizing the progress bar before the first byte transfers.
    [[nodiscard]] static juce::int64 expectedTotalBytes();

    // Begin a download on a background thread. The callback fires on the
    // message thread after each progress tick and once on completion
    // (Done / Failed / Cancelled). No-op if a download is already in
    // flight.
    void start(ProgressCallback onProgress);

    // Request the worker to stop at the next chunk boundary. Callback
    // will fire once more with phase == Cancelled.
    void cancel();

    [[nodiscard]] bool isRunning() const noexcept;

  private:
    class Worker;
    std::unique_ptr<Worker> worker_;
};

}  // namespace magda::media
