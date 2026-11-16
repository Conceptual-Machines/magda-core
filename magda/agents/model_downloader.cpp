// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "model_downloader.hpp"

#include <juce_cryptography/juce_cryptography.h>
#include <juce_events/juce_events.h>

#include "../daw/core/AppPaths.hpp"
#include "../daw/core/Config.hpp"
#include "ranged_download.hpp"

namespace magda {

namespace {

// Pinned to a revision: /resolve/<commit>/ always serves these bytes, so the size and hash hold.
constexpr const char* kRepo = "ConceptualMachines/magda-gguf";
constexpr const char* kRevision = "d7b1fdb796eda6d2a4f3ef245465ee3951b78dbc";
constexpr const char* kFileName = "magda-v0.4.0-q4_k_m.gguf";
constexpr juce::int64 kSize = 4683073568;
constexpr const char* kSha256 = "737723596dfcf4f8df9e1bf45057c1857298cebd70da45a6ddbb650e700fd42a";

/// For a CDN that caps each connection (1.3 MB/s measured in #1830); a fast line fills with one.
constexpr int kConnections = 6;

juce::String modelUrl() {
    return juce::String("https://huggingface.co/") + kRepo + "/resolve/" + kRevision + "/" +
           kFileName;
}

juce::File partFileFor(const juce::File& target) {
    return target.getSiblingFile(target.getFileName() + ".part");
}

juce::File stateFileFor(const juce::File& target) {
    return target.getSiblingFile(target.getFileName() + ".part.json");
}

/// Names an unfinished download's target between launches.
juce::File pendingFile() {
    return paths::dataDir().getChildFile("LocalModel").getChildFile("pending-download.txt");
}

juce::File pendingTarget() {
    const auto path = pendingFile().loadFileAsString().trim();
    return juce::File::isAbsolutePath(path) ? juce::File(path) : juce::File();
}

void rememberPending(const juce::File& target) {
    pendingFile().getParentDirectory().createDirectory();
    pendingFile().replaceWithText(target.getFullPathName());
}

void forgetPending() {
    pendingFile().deleteFile();
}

void deletePartial(const juce::File& target) {
    partFileFor(target).deleteFile();
    stateFileFor(target).deleteFile();
}

void useModel(const juce::File& target) {
    juce::MessageManager::callAsync([path = target.getFullPathName().toStdString()] {
        auto& config = Config::getInstance();
        config.setLocalModelPath(path);
        config.save();
    });
}

ModelDownloadManager::Failure failureOf(download::RangedDownload::Failure failure) {
    switch (failure) {
        case download::RangedDownload::Failure::Unreachable:
            return ModelDownloadManager::Failure::Unreachable;
        case download::RangedDownload::Failure::Disk:
            return ModelDownloadManager::Failure::Disk;
        case download::RangedDownload::Failure::Network:
        case download::RangedDownload::Failure::None:
            break;
    }
    return ModelDownloadManager::Failure::Network;
}

/** @brief Counts the bytes read through it, and ends early once @p stop is set. */
class CountingStream final : public juce::InputStream {
  public:
    CountingStream(juce::InputStream& source, std::atomic<juce::int64>& count,
                   const std::atomic<bool>& stop)
        : source_(source), count_(count), stop_(stop) {}

    juce::int64 getTotalLength() override {
        return source_.getTotalLength();
    }

    bool isExhausted() override {
        return stop_ || source_.isExhausted();
    }

    int read(void* dest, int maxBytes) override {
        if (stop_)
            return 0;
        const int read = source_.read(dest, maxBytes);
        count_ += std::max(read, 0);
        return read;
    }

    juce::int64 getPosition() override {
        return source_.getPosition();
    }

    bool setPosition(juce::int64 position) override {
        return source_.setPosition(position);
    }

  private:
    juce::InputStream& source_;
    std::atomic<juce::int64>& count_;
    const std::atomic<bool>& stop_;
};

}  // namespace

class ModelDownloadManager::Job final : public juce::Thread {
  public:
    Job(ModelDownloadManager& owner, juce::File target)
        : juce::Thread("MAGDA Model Download"), owner_(owner), target_(std::move(target)) {}

    void run() override {
        owner_.runJob(target_);
    }

  private:
    ModelDownloadManager& owner_;
    juce::File target_;
};

bool ModelDownloadManager::Status::running() const noexcept {
    return transferring() || phase == Phase::Stopping || phase == Phase::Verifying;
}

bool ModelDownloadManager::Status::transferring() const noexcept {
    return phase == Phase::Connecting || phase == Phase::Downloading;
}

double ModelDownloadManager::Status::secondsLeft() const noexcept {
    if (bytesPerSecond <= 0.0 || bytesTotal <= 0)
        return -1.0;
    return static_cast<double>(bytesTotal - bytesDone) / bytesPerSecond;
}

ModelDownloadManager& ModelDownloadManager::getInstance() {
    static ModelDownloadManager instance;
    return instance;
}

ModelDownloadManager::ModelDownloadManager() {
    const auto target = pendingTarget();
    if (target == juce::File())
        return;

    const auto saved = download::resumeStateFromJson(stateFileFor(target).loadFileAsString());
    if (!saved || !partFileFor(target).existsAsFile()) {
        forgetPending();
        return;
    }

    status_.phase = Phase::Paused;
    status_.target = target;
    status_.bytesDone = saved->bytesDone();
    status_.bytesTotal = saved->size;
    status_.resumable = true;
}

ModelDownloadManager::~ModelDownloadManager() {
    shutdown();
}

juce::String ModelDownloadManager::fileName() {
    return kFileName;
}

ModelDownloadManager::Status ModelDownloadManager::status() const {
    std::lock_guard lock(lock_);
    auto status = status_;
    status.bytesChecked = bytesChecked_.load();
    return status;
}

void ModelDownloadManager::start(const juce::File& target) {
    const auto current = status();
    if (current.running())
        return;

    if (current.resumable && current.target != target)
        deletePartial(current.target);

    if (target.existsAsFile() && target.getSize() == kSize) {
        forgetPending();
        {
            std::lock_guard lock(lock_);
            status_ = {};
            status_.phase = Phase::Done;
            status_.target = target;
        }
        useModel(target);
        return;
    }

    launch(target);
}

void ModelDownloadManager::resume() {
    const auto current = status();
    if (!current.running() && current.resumable)
        launch(current.target);
}

void ModelDownloadManager::cancel() {
    std::lock_guard lock(lock_);
    if (!status_.running())
        return;

    stopRequested_ = true;
    if (transfer_ != nullptr)
        transfer_->stop();
    status_.phase = Phase::Stopping;
}

void ModelDownloadManager::discard() {
    const auto current = status();
    if (current.running() || !current.resumable)
        return;

    deletePartial(current.target);
    forgetPending();
    std::lock_guard lock(lock_);
    status_ = {};
}

void ModelDownloadManager::shutdown() {
    cancel();
    if (job_ != nullptr) {
        job_->stopThread(10000);
        job_.reset();
    }
}

void ModelDownloadManager::launch(const juce::File& target) {
    // The last job has already reported its end; this only waits for its thread to return.
    if (job_ != nullptr) {
        job_->waitForThreadToExit(-1);
        job_.reset();
    }

    rememberPending(target);
    stopRequested_ = false;
    bytesChecked_ = 0;
    lastSampleMs_ = 0.0;
    {
        std::lock_guard lock(lock_);
        if (status_.target != target) {
            status_.bytesDone = 0;
            status_.bytesTotal = 0;
        }
        status_.phase = Phase::Connecting;
        status_.failure = Failure::None;
        status_.target = target;
        status_.bytesPerSecond = 0.0;
        status_.resumable = false;
    }

    job_ = std::make_unique<Job>(*this, target);
    job_->startThread();
}

void ModelDownloadManager::runJob(const juce::File& target) {
    const auto part = partFileFor(target);
    const auto source = download::httpSource(modelUrl());
    download::RangedDownload transfer(*source, part, stateFileFor(target), kConnections);
    {
        std::lock_guard lock(lock_);
        transfer_ = &transfer;
        if (stopRequested_)
            transfer.stop();
    }

    const auto result = transfer.run([this, &transfer] { sample(transfer); });
    {
        std::lock_guard lock(lock_);
        transfer_ = nullptr;
        status_.bytesDone = transfer.bytesDone();
    }

    using Outcome = download::RangedDownload::Outcome;
    if (result.outcome == Outcome::Stopped) {
        const bool kept = part.existsAsFile();
        return finish(kept ? Phase::Paused : Phase::Idle, Failure::None, kept);
    }
    if (result.outcome == Outcome::Failed)
        return finish(Phase::Failed, failureOf(result.failure), part.existsAsFile());

    {
        std::lock_guard lock(lock_);
        if (status_.phase != Phase::Stopping)
            status_.phase = Phase::Verifying;
    }

    switch (verify(part)) {
        case Check::Stopped:
            return finish(Phase::Paused, Failure::None, true);
        case Check::Damaged:
            deletePartial(target);
            return finish(Phase::Failed, Failure::Damaged, false);
        case Check::Intact:
            break;
    }

    if (!part.moveFileTo(target))
        return finish(Phase::Failed, Failure::Move, true);

    stateFileFor(target).deleteFile();
    forgetPending();
    useModel(target);
    finish(Phase::Done, Failure::None, false);
}

void ModelDownloadManager::sample(const download::RangedDownload& transfer) {
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto bytes = transfer.bytesDone();

    std::lock_guard lock(lock_);
    status_.bytesDone = bytes;
    status_.bytesTotal = transfer.bytesTotal();
    if (status_.phase == Phase::Connecting && status_.bytesTotal > 0)
        status_.phase = Phase::Downloading;

    if (lastSampleMs_ > 0.0 && now > lastSampleMs_) {
        const auto rate =
            static_cast<double>(bytes - lastSampleBytes_) * 1000.0 / (now - lastSampleMs_);
        // Smoothed over a few seconds so the time left does not jump with every read.
        status_.bytesPerSecond =
            status_.bytesPerSecond > 0.0 ? 0.9 * status_.bytesPerSecond + 0.1 * rate : rate;
    }
    lastSampleMs_ = now;
    lastSampleBytes_ = bytes;
}

void ModelDownloadManager::finish(Phase phase, Failure failure, bool resumable) {
    if (!resumable && phase != Phase::Done)
        forgetPending();

    std::lock_guard lock(lock_);
    status_.phase = phase;
    status_.failure = failure;
    status_.resumable = resumable;
    status_.bytesPerSecond = 0.0;
}

ModelDownloadManager::Check ModelDownloadManager::verify(const juce::File& part) {
    if (part.getSize() != kSize)
        return Check::Damaged;

    juce::FileInputStream file(part);
    if (!file.openedOk())
        return Check::Damaged;

    // SHA256 reads 64 bytes at a time; the buffer keeps that off the disk.
    CountingStream counted(file, bytesChecked_, stopRequested_);
    juce::BufferedInputStream buffered(counted, 1 << 20);
    const juce::SHA256 hash(buffered);

    if (stopRequested_)
        return Check::Stopped;
    return hash.toHexString().equalsIgnoreCase(kSha256) ? Check::Intact : Check::Damaged;
}

}  // namespace magda
