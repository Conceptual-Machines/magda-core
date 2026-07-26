// Downloads the encoder command model from HuggingFace into the app's data
// dir (#1847), mirroring stem_separation/StemModelDownloader.
//
// The bundle is ~450 MB, so it is never shipped inside the app: the 51k conv
// net stays built in as the always-available fallback, and this fetches the
// bigger, far more accurate model only when the user asks. On held-out
// phrasing the conv net scores 46.5% and this scores 91.5%.
//
// Files land in CommandModelOnnx::defaultAssetDir(), which may be a
// user-selected folder. Threading matches the stem downloader: start() spawns
// a background juce::Thread, callbacks hop to the message thread via
// callAsync.

#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>
#include <vector>

namespace magda {

class CommandModelDownloader {
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

    CommandModelDownloader();
    ~CommandModelDownloader();

    CommandModelDownloader(const CommandModelDownloader&) = delete;
    CommandModelDownloader& operator=(const CommandModelDownloader&) = delete;

    /** Where the bundle lives — CommandModelOnnx::defaultAssetDir(). */
    [[nodiscard]] static juce::File modelsDir();

    /** Bundle files on disk in manifest order, whether or not they exist. */
    [[nodiscard]] static std::vector<juce::File> modelFiles();

    [[nodiscard]] static const char* displayName();

    /** Human-facing HuggingFace repo page, shown as a link in the UI. */
    [[nodiscard]] static const char* sourceUrl();

    /** Existence + size check only; skips hashing so the UI can poll it. */
    [[nodiscard]] static bool isInstalled();

    [[nodiscard]] static juce::int64 expectedTotalBytes();

    /** Delete the installed bundle. False if removal failed. */
    static bool remove();

    /** Begin downloading on a background thread. No-op if already running. */
    void start(ProgressCallback onProgress);

    /** Stop at the next chunk boundary; fires once more with Cancelled. */
    void cancel();

    [[nodiscard]] bool isRunning() const noexcept;

  private:
    class Worker;
    std::unique_ptr<Worker> worker_;
};

}  // namespace magda
