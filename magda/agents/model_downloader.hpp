#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>

namespace magda {

/**
 * @brief Downloads a GGUF model file from a URL with progress reporting.
 *
 * Uses JUCE's URL::downloadToFile() with DownloadTaskListener for async
 * download with progress callbacks.
 */
class ModelDownloader : private juce::URL::DownloadTaskListener {
  public:
    using ProgressCallback = std::function<void(int64_t bytesDownloaded, int64_t totalBytes)>;
    using CompletionCallback = std::function<void(bool success, const juce::String& modelPath)>;

    ModelDownloader() = default;
    ~ModelDownloader() override;

    /**
     * @brief Start downloading a model from a URL to a user-chosen location.
     * @param url        Full URL to the .gguf file
     * @param targetFile Where to save the downloaded file
     * @param onProgress Called on download thread with byte counts
     * @param onComplete Called on download thread when finished (success + local path)
     */
    void startDownload(const juce::String& url, const juce::File& targetFile,
                       ProgressCallback onProgress, CompletionCallback onComplete);

    /** Cancel an in-progress download. */
    void cancel();

    bool isDownloading() const {
        return downloading_.load();
    }

    /** Default download URL for the MAGDA model. */
    static juce::String getDefaultModelUrl();

  private:
    void finished(juce::URL::DownloadTask* task, bool success) override;
    void progress(juce::URL::DownloadTask* task, juce::int64 bytesDownloaded,
                  juce::int64 totalLength) override;

    std::unique_ptr<juce::URL::DownloadTask> downloadTask_;
    juce::File targetFile_;
    std::atomic<bool> downloading_{false};
    juce::int64 expectedSize_{-1};
    ProgressCallback progressCallback_;
    CompletionCallback completionCallback_;
};

}  // namespace magda
