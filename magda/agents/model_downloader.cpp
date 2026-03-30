#include "model_downloader.hpp"

namespace magda {

ModelDownloader::~ModelDownloader() {
    cancel();
}

juce::String ModelDownloader::getDefaultModelUrl() {
    return "https://huggingface.co/ConceptualMachines/magda-gguf/resolve/main/"
           "magda-v0.3.0-q4_k_m.gguf";
}

void ModelDownloader::startDownload(const juce::String& url, const juce::File& targetFile,
                                    ProgressCallback onProgress, CompletionCallback onComplete) {
    if (downloading_.load())
        return;

    progressCallback_ = std::move(onProgress);
    completionCallback_ = std::move(onComplete);
    downloading_ = true;
    targetFile_ = targetFile;

    // If file already exists, report success immediately
    if (targetFile_.existsAsFile()) {
        downloading_ = false;
        if (completionCallback_)
            completionCallback_(true, targetFile_.getFullPathName());
        return;
    }

    // Probe Content-Length via a HEAD request (follows redirects).
    // Some platforms (Windows WinINet) don't report the length during download
    // after a 302 redirect, so we cache it here for the progress callback.
    expectedSize_ = -1;
    auto headStream = juce::URL(url).createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withHttpRequestCmd("HEAD")
            .withNumRedirectsToFollow(5)
            .withConnectionTimeoutMs(5000));
    if (headStream)
        expectedSize_ = headStream->getTotalLength();

    // Ensure parent directory exists
    targetFile_.getParentDirectory().createDirectory();

    // Download to a temp file, rename on completion
    auto tempFile = targetFile_.withFileExtension(".download");

    juce::URL::DownloadTaskOptions options;
    options = options.withListener(this);

    downloadTask_ = juce::URL(url).downloadToFile(tempFile, options);

    if (!downloadTask_) {
        downloading_ = false;
        if (completionCallback_)
            completionCallback_(false, {});
    }
}

void ModelDownloader::cancel() {
    downloadTask_.reset();
    downloading_ = false;

    // Clean up partial download
    auto tempFile = targetFile_.withFileExtension(".download");
    tempFile.deleteFile();
}

void ModelDownloader::progress(juce::URL::DownloadTask* /*task*/, juce::int64 bytesDownloaded,
                               juce::int64 totalLength) {
    // Fall back to the size obtained from the HEAD request if the download
    // task doesn't report Content-Length (e.g. Windows after a redirect).
    if (totalLength <= 0 && expectedSize_ > 0)
        totalLength = expectedSize_;

    if (progressCallback_)
        progressCallback_(bytesDownloaded, totalLength);
}

void ModelDownloader::finished(juce::URL::DownloadTask* task, bool success) {
    downloading_ = false;

    auto tempFile = targetFile_.withFileExtension(".download");

    if (success && !task->hadError()) {
        tempFile.moveFileTo(targetFile_);
        if (completionCallback_)
            completionCallback_(true, targetFile_.getFullPathName());
    } else {
        tempFile.deleteFile();
        if (completionCallback_)
            completionCallback_(false, {});
    }
}

}  // namespace magda
