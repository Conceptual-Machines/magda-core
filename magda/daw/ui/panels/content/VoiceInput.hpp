// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file VoiceInput.hpp
 * @brief The AI console's mic: hold to record, release to transcribe.
 */

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace magda {
class AudioIOControl;
}  // namespace magda

namespace magda::daw::ui {

/**
 * @brief Every open input summed to mono, appended by the audio thread.
 *
 * Sized by prepare() before the device runs; append() drops what does not fit.
 */
class MonoCapture {
  public:
    void prepare(int capacity);

    /// Audio thread.
    void append(const float* const* inputs, int numInputs, int numSamples);

    int size() const {
        return written_.load(std::memory_order_acquire);
    }
    const float* data() const {
        return samples_.data();
    }

  private:
    std::vector<float> samples_;
    std::atomic<int> written_{0};
};

/**
 * @brief Records the open inputs between press() and release(), then transcribes them.
 *
 * A reader on AudioIOControl like HardwareInputLevels, attached only while recording, so the
 * buffer is read once no callback can be running. The decode has its own thread; both
 * callbacks arrive on the message thread.
 */
class VoiceInput final : private juce::AudioIODeviceCallback, private juce::ChangeListener {
  public:
    /// Where things stand, for a status line. Empty clears it.
    std::function<void(const juce::String&)> onStatus;
    std::function<void(const juce::String&)> onTranscript;

    VoiceInput();
    ~VoiceInput() override;

    /** @brief Load a downloaded model in the background, so the first press is as quick. */
    void preload();

    /** @brief Start recording, or the model download when the model is missing. */
    void press();

    /** @brief Stop recording and transcribe it, biased towards the words in @p prompt. */
    void release(const juce::String& prompt);

    /** @brief press() or release(), for a key that sends one event per press. */
    void toggle(const juce::String& prompt);

    bool isRecording() const {
        return audio_ != nullptr;
    }

    /** @brief Recording, downloading the model or transcribing. */
    bool isBusy() const;

  private:
    class Worker;

    void audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs,
                                          float* const* outputs, int numOutputs, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override {}

    /** @brief Report the model's download in the status line. */
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    void finishTranscription(const juce::String& text, const juce::String& error);
    void status(const juce::String& text);

    AudioIOControl* audio_ = nullptr;  ///< Set while recording.
    MonoCapture capture_;
    double sampleRate_ = 0.0;
    int numInputs_ = 0;
    std::unique_ptr<Worker> worker_;
    bool showingDownload_ = false;  ///< The status line is the download's.
    juce::uint32 lastToggleMs_ = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE(VoiceInput)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceInput)
};

}  // namespace magda::daw::ui
