// Downloads the media DB's ONNX model bundles from HuggingFace into the app's
// data dir (issues #768, #2674).
//
// Two bundles, installable independently because they buy different things:
//
//   Sample tagger  three files, ~595 MB: the CLAP audio and text encoders
//                  and the RoBERTa tokenizer. Without them the media DB
//                  falls back to FTS-only filename and tag search.
//   Beat tracker   one file, ~79 MB: Beat This!, which measures a tempo for
//                  a file whose name and header do not carry one. Without it
//                  that tier falls back to an autocorrelation that is right
//                  far less often.
//
// Both land in MediaDbContext::modelsDir(), and the lazy-load paths that read
// them pick them up on next access -- no restart required.
//
// Hosted under ConceptualMachines on HuggingFace; URLs and expected SHA-256s
// are baked into the implementation's manifest so a caller-side tampering
// check is automatic.
//
// Threading: start() spawns a background juce::Thread. Progress and
// completion callbacks fire on the MAGDA message thread via callAsync.

#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <memory>

namespace magda::media {

class MediaModelDownloader {
  public:
    /// An independently installable set of model files.
    enum class Bundle {
        SampleTagger,  ///< CLAP encoders + tokenizer, for semantic search.
        BeatTracker,   ///< Beat This!, for the measured BPM tier.
    };

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

    explicit MediaModelDownloader(Bundle bundle);
    ~MediaModelDownloader();

    MediaModelDownloader(const MediaModelDownloader&) = delete;
    MediaModelDownloader& operator=(const MediaModelDownloader&) = delete;

    // Quick existence check — every manifest file is present in
    // MediaDbContext::modelsDir() with the expected size. Skips the
    // hash check (expensive on a 500 MB file) so callers can use this
    // every time the DB browser repaints without burning CPU.
    [[nodiscard]] static bool isInstalled(Bundle bundle);

    // Total bytes the bundle will occupy on disk once downloaded. Useful
    // for sizing the progress bar before the first byte transfers.
    [[nodiscard]] static juce::int64 expectedTotalBytes(Bundle bundle);

    /// What to call the bundle in the download UI.
    [[nodiscard]] static const char* displayName(Bundle bundle);

    /// The HuggingFace repo page the files come from, for the UI to link.
    [[nodiscard]] static const char* sourceUrl(Bundle bundle);

    /// Delete the installed files. False when a removal failed.
    static bool remove(Bundle bundle);

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
    Bundle bundle_;
    std::unique_ptr<Worker> worker_;
};

}  // namespace magda::media
