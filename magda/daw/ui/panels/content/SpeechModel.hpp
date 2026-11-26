// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file SpeechModel.hpp
 * @brief The speech model's download and load, for every feature that shows its state.
 */

#pragma once

#include <juce_events/juce_events.h>

#include <atomic>
#include <memory>

namespace magda::daw::ui {

/**
 * @brief Downloads and loads the speech model for everything that shows its state.
 *
 * Message thread. One download and one load or unload at a time; every step, download
 * progress included, sends a change message.
 */
class SpeechModel final : public juce::ChangeBroadcaster {
  public:
    static SpeechModel& getInstance();
    ~SpeechModel() override;

    /** @brief Cancel the download and wait out a load, before the app tears down. */
    void shutdown();

    bool isDownloaded() const;
    bool isDownloading() const;
    /// 0 to 1 while downloading, 0 until the size is known.
    double downloadProgress() const {
        return progress_.load(std::memory_order_relaxed);
    }
    /// Loading or unloading.
    bool isBusy() const {
        return job_ != nullptr;
    }
    bool isLoading() const {
        return job_ != nullptr && jobLoads_;
    }
    bool isLoaded() const;

    /** @brief Why the last download or load failed, or empty. */
    const juce::String& lastError() const {
        return error_;
    }

    /** @brief Download the model, then load it. */
    void download();
    void cancelDownload();

    /** @brief Delete the model's file and unload it. */
    void remove();

    /** @brief Load in the background and warm up, so the first transcription is quick. */
    void load();
    void unload();

    /** @brief Tell listeners something outside this class changed, such as a lazy load. */
    void notify() {
        sendChangeMessage();
    }

  private:
    SpeechModel();

    class Job;
    class Download;
    void startJob(bool loading);
    void jobFinished(const juce::String& error);
    void downloadFinished(const juce::String& error);

    std::unique_ptr<Download> download_;
    int downloadId_ = 0;  ///< Bumped on cancel, so a stale completion is dropped.
    std::unique_ptr<Job> job_;
    bool jobLoads_ = false;
    std::atomic<double> progress_{0.0};
    juce::String error_;

    JUCE_DECLARE_NON_COPYABLE(SpeechModel)
};

}  // namespace magda::daw::ui
