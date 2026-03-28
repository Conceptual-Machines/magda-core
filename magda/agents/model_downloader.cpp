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
