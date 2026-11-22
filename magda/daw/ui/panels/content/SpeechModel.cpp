// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SpeechModel.hpp"

#include "../../../../agents/model_downloader.hpp"
#include "../../../../agents/speech_transcriber.hpp"

namespace magda::daw::ui {

class SpeechModel::Job final : public juce::Thread {
  public:
    explicit Job(bool loading)
        : juce::Thread(loading ? "Speech model load" : "Speech model unload"), loading_(loading) {}

    ~Job() override {
        abort_.store(true, std::memory_order_relaxed);
        stopThread(10000);
    }

    void run() override {
        auto& transcriber = SpeechTranscriber::getInstance();
        juce::String error;
        if (loading_)
            error = transcriber.preload(&abort_);
        else
            transcriber.unload();
        if (abort_.load(std::memory_order_relaxed))
            return;

        juce::MessageManager::callAsync([error] { getInstance().jobFinished(error); });
    }

  private:
    const bool loading_;
    std::atomic<bool> abort_{false};
};

SpeechModel& SpeechModel::getInstance() {
    static SpeechModel instance;
    return instance;
}

SpeechModel::SpeechModel() = default;

SpeechModel::~SpeechModel() {
    shutdown();
}

void SpeechModel::shutdown() {
    ++downloadId_;
    downloader_.reset();
    job_.reset();
}

bool SpeechModel::isDownloaded() const {
    return SpeechTranscriber::modelFile().existsAsFile();
}

bool SpeechModel::isDownloading() const {
    return downloader_ != nullptr && downloader_->isDownloading();
}

bool SpeechModel::isLoaded() const {
    return SpeechTranscriber::getInstance().isLoaded();
}

void SpeechModel::download() {
    if (isDownloading() || isDownloaded() || !SpeechTranscriber::isAvailable())
        return;

    error_ = {};
    progress_.store(0.0, std::memory_order_relaxed);
    downloader_ = std::make_unique<ModelDownloader>();
    const int id = ++downloadId_;

    // The download thread reports; sendChangeMessage() is safe from any thread.
    downloader_->startDownload(
        SpeechTranscriber::modelUrl(), SpeechTranscriber::modelFile(),
        [this](int64_t done, int64_t total) {
            if (total > 0)
                progress_.store(static_cast<double>(done) / static_cast<double>(total),
                                std::memory_order_relaxed);
            sendChangeMessage();
        },
        [id](bool success, const juce::String&) {
            juce::MessageManager::callAsync([id, success] {
                auto& self = getInstance();
                if (id == self.downloadId_)
                    self.downloadFinished(success);
            });
        });
    sendChangeMessage();
}

void SpeechModel::cancelDownload() {
    ++downloadId_;
    downloader_.reset();
    progress_.store(0.0, std::memory_order_relaxed);
    sendChangeMessage();
}

void SpeechModel::downloadFinished(bool success) {
    downloader_.reset();
    error_ = success ? juce::String() : juce::String("The speech model could not be downloaded.");
    sendChangeMessage();
    if (success)
        load();
}

void SpeechModel::remove() {
    if (job_ != nullptr)
        return;
    SpeechTranscriber::modelFile().deleteFile();
    error_ = {};
    if (isLoaded())
        startJob(false);
    sendChangeMessage();
}

void SpeechModel::load() {
    if (job_ == nullptr && isDownloaded() && !isLoaded())
        startJob(true);
}

void SpeechModel::unload() {
    if (job_ == nullptr && isLoaded())
        startJob(false);
}

void SpeechModel::startJob(bool loading) {
    error_ = {};
    jobLoads_ = loading;
    job_ = std::make_unique<Job>(loading);
    job_->startThread(juce::Thread::Priority::low);
    sendChangeMessage();
}

void SpeechModel::jobFinished(const juce::String& error) {
    job_.reset();
    error_ = error;
    sendChangeMessage();
}

}  // namespace magda::daw::ui
