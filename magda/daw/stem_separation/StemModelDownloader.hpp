// Downloads stem-separation model weights from HuggingFace into the app's
// data dir (issue #1288), mirroring media_db/SampleTaggerDownloader.
//
// Weights are fetched at the user's explicit request, never bundled: the
// Demucs weights are published by Meta for research use, so MAGDA ships no
// copy and downloads only when the user asks (the posture Audacity and
// StemRoller take). URLs, sizes, and SHA-256s are baked into the
// implementation's manifest so a tampering check is automatic.
//
// Models land in dataDir()/StemSeparation/models/. Threading: start()
// spawns a background juce::Thread; progress and completion callbacks fire
// on the message thread via callAsync.

#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>

namespace magda::stems {

// Downloadable model bundles. Spleeter joins this enum with its own entry.
enum class StemModel {
    Htdemucs,  // Demucs v4 hybrid transformer, 4 stems, fp32 ONNX (~316 MB)
};

class StemModelDownloader {
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
        juce::String currentFilename;
        juce::int64 bytesDone = 0;
        juce::int64 bytesTotal = 0;
        juce::String errorMessage;  // populated when phase == Failed
    };

    using ProgressCallback = std::function<void(const Progress&)>;

    explicit StemModelDownloader(StemModel model);
    ~StemModelDownloader();

    StemModelDownloader(const StemModelDownloader&) = delete;
    StemModelDownloader& operator=(const StemModelDownloader&) = delete;

    // Where all stem-separation models live.
    [[nodiscard]] static juce::File modelsDir();

    // The model's .onnx file on disk (whether or not it exists yet).
    [[nodiscard]] static juce::File modelFile(StemModel model);

    [[nodiscard]] static const char* displayName(StemModel model);

    // Existence + size check only; skips the hash so UI can poll it cheaply.
    [[nodiscard]] static bool isInstalled(StemModel model);

    [[nodiscard]] static juce::int64 expectedTotalBytes(StemModel model);

    // Delete the installed weights. Returns false if removal failed.
    static bool remove(StemModel model);

    // Begin a download on a background thread. The callback fires on the
    // message thread after each progress tick and once on completion.
    // No-op if a download is already in flight.
    void start(ProgressCallback onProgress);

    // Request the worker to stop at the next chunk boundary. Callback
    // fires once more with phase == Cancelled.
    void cancel();

    [[nodiscard]] bool isRunning() const noexcept;

  private:
    class Worker;
    StemModel model_;
    std::unique_ptr<Worker> worker_;
};

}  // namespace magda::stems
