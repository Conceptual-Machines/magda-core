// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file model_downloader.hpp
 * @brief The Local LLM model download, owned by the app so it outlives Settings (#1830).
 */

#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>
#include <mutex>

namespace magda {

namespace download {
class RangedDownload;
}

/** @brief Downloads the pinned MAGDA model in the background, resumable across launches. */
class ModelDownloadManager {
  public:
    enum class Phase { Idle, Connecting, Downloading, Stopping, Paused, Verifying, Done, Failed };
    enum class Failure { None, Unreachable, Network, Disk, Damaged, Move };

    struct Status {
        Phase phase = Phase::Idle;
        Failure failure = Failure::None;
        juce::File target;
        juce::int64 bytesDone = 0;
        juce::int64 bytesTotal = 0;
        /// Bytes of the finished file hashed so far, while Verifying.
        juce::int64 bytesChecked = 0;
        double bytesPerSecond = 0.0;
        /// A partial file is waiting to be resumed or discarded.
        bool resumable = false;

        [[nodiscard]] bool running() const noexcept;
        /// Whether Cancel applies: the transfer itself, not the check after it.
        [[nodiscard]] bool transferring() const noexcept;
        /// -1 while the rate is unknown.
        [[nodiscard]] double secondsLeft() const noexcept;
    };

    static ModelDownloadManager& getInstance();

    ModelDownloadManager(const ModelDownloadManager&) = delete;
    ModelDownloadManager& operator=(const ModelDownloadManager&) = delete;

    [[nodiscard]] static juce::String fileName();

    /// Any thread.
    [[nodiscard]] Status status() const;

    /**
     * @brief Download into @p target, replacing a partial download elsewhere. Message thread.
     *
     * A file already at @p target with the model's size is taken as it is.
     */
    void start(const juce::File& target);

    /// Continue a paused or failed download. Message thread.
    void resume();

    /// Stop, keeping what has downloaded. Any thread.
    void cancel();

    /// Delete a paused download's partial file. Message thread.
    void discard();

    /// App quit: stop and wait, keeping the partial file.
    void shutdown();

  private:
    class Job;

    ModelDownloadManager();
    ~ModelDownloadManager();

    void launch(const juce::File& target);
    void runJob(const juce::File& target);
    void sample(const download::RangedDownload& transfer);
    void finish(Phase phase, Failure failure, bool resumable);

    enum class Check { Intact, Damaged, Stopped };
    Check verify(const juce::File& part);

    mutable std::mutex lock_;
    Status status_;
    download::RangedDownload* transfer_ = nullptr;

    std::unique_ptr<Job> job_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<juce::int64> bytesChecked_{0};
    double lastSampleMs_ = 0.0;
    juce::int64 lastSampleBytes_ = 0;
};

}  // namespace magda
