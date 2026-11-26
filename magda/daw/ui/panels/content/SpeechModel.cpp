// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SpeechModel.hpp"

#include "../../../../agents/ranged_download.hpp"
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

namespace {

constexpr int kConnections = 4;

juce::File partFile() {
    const auto target = SpeechTranscriber::modelFile();
    return target.getSiblingFile(target.getFileName() + ".part");
}

juce::File stateFile() {
    const auto target = SpeechTranscriber::modelFile();
    return target.getSiblingFile(target.getFileName() + ".part.json");
}

/// Moves a finished transfer into place; the reason it could not, or empty.
juce::String install(const download::RangedDownload::Result& result) {
    if (result.outcome != download::RangedDownload::Outcome::Complete)
        return "The speech model could not be downloaded.";

    const auto part = partFile();
    if (part.getSize() != SpeechTranscriber::kModelBytes) {
        part.deleteFile();
        stateFile().deleteFile();
        return "The speech model download was damaged. Try again.";
    }
    if (!part.moveFileTo(SpeechTranscriber::modelFile()))
        return "The speech model could not be saved.";
    stateFile().deleteFile();
    return {};
}

}  // namespace

/// A stopped download keeps its partial file, and the next one resumes it.
class SpeechModel::Download final : public juce::Thread {
  public:
    explicit Download(int id)
        : juce::Thread("Speech model download"),
          id_(id),
          source_(download::httpSource(SpeechTranscriber::modelUrl())),
          transfer_(*source_, partFile(), stateFile(), kConnections) {}

    ~Download() override {
        transfer_.stop();
        stopThread(10000);
    }

    void run() override {
        auto& model = getInstance();
        const auto result = transfer_.run([this, &model] {
            if (const auto total = transfer_.bytesTotal(); total > 0)
                model.progress_.store(static_cast<double>(transfer_.bytesDone()) /
                                          static_cast<double>(total),
                                      std::memory_order_relaxed);
            model.sendChangeMessage();
        });
        if (result.outcome == download::RangedDownload::Outcome::Stopped)
            return;

        juce::MessageManager::callAsync([id = id_, error = install(result)] {
            auto& self = getInstance();
            if (id == self.downloadId_)
                self.downloadFinished(error);
        });
    }

  private:
    const int id_;
    std::unique_ptr<download::Source> source_;
    download::RangedDownload transfer_;
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
    download_.reset();
    job_.reset();
}

bool SpeechModel::isDownloaded() const {
    return SpeechTranscriber::modelFile().existsAsFile();
}

bool SpeechModel::isDownloading() const {
    return download_ != nullptr;
}

bool SpeechModel::isLoaded() const {
    return SpeechTranscriber::getInstance().isLoaded();
}

void SpeechModel::download() {
    if (isDownloading() || isDownloaded() || !SpeechTranscriber::isAvailable())
        return;

    error_ = {};
    progress_.store(0.0, std::memory_order_relaxed);
    SpeechTranscriber::modelFile().getParentDirectory().createDirectory();
    download_ = std::make_unique<Download>(++downloadId_);
    download_->startThread(juce::Thread::Priority::low);
    sendChangeMessage();
}

void SpeechModel::cancelDownload() {
    ++downloadId_;
    download_.reset();
    progress_.store(0.0, std::memory_order_relaxed);
    sendChangeMessage();
}

void SpeechModel::downloadFinished(const juce::String& error) {
    download_.reset();
    error_ = error;
    sendChangeMessage();
    if (error.isEmpty())
        load();
}

void SpeechModel::remove() {
    if (job_ != nullptr)
        return;
    cancelDownload();
    SpeechTranscriber::modelFile().deleteFile();
    partFile().deleteFile();
    stateFile().deleteFile();
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
