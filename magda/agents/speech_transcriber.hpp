// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file speech_transcriber.hpp
 * @brief Local speech to text on whisper.cpp.
 */

#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

struct whisper_context;

namespace magda {

/**
 * @brief One loaded whisper model, kept between transcriptions.
 *
 * Builds without local inference get a stub whose transcribe() always fails.
 */
class SpeechTranscriber {
  public:
    /// The rate whisper takes its input at.
    static constexpr int kSampleRate = 16000;

    struct Result {
        std::string text;
        std::string error;  ///< Empty on success.
    };

    static SpeechTranscriber& getInstance();
    ~SpeechTranscriber();

    SpeechTranscriber(const SpeechTranscriber&) = delete;
    SpeechTranscriber& operator=(const SpeechTranscriber&) = delete;

    /** @brief False in builds without local inference, where transcribe() always fails. */
    static bool isAvailable();

    /** @brief The folder set in Settings, or the default under the app's data folder. */
    static juce::File modelsDir();

    /** @brief Where the model is kept, whether or not it is downloaded. */
    static juce::File modelFile();

    /** @brief Where modelFile() comes from. */
    static juce::String modelUrl();

    /// modelFile()'s size, for a download offer.
    static constexpr juce::int64 kModelBytes = 190098681;

    /**
     * @brief Transcribe kSampleRate mono @p samples, loading the model on first use.
     *
     * Blocks for the whole decode, so never on the message thread. @p prompt biases the
     * vocabulary towards words it contains, such as the project's track names. Setting
     * @p abort ends the decode early with an error.
     */
    Result transcribe(const std::vector<float>& samples, const std::string& prompt,
                      const std::atomic<bool>* abort = nullptr);

    /**
     * @brief Load the model and decode a second of silence, so the first transcribe() pays
     * for neither. Blocks like transcribe(); returns why it could not load, or empty.
     */
    std::string preload(const std::atomic<bool>* abort = nullptr);

    void unload();

    bool isLoaded() const {
        return loaded_.load(std::memory_order_acquire);
    }

    /** @brief @p numSamples of mono @p input at @p sampleRate, resampled to kSampleRate. */
    static std::vector<float> toWhisperRate(const float* input, int numSamples, double sampleRate);

  private:
    SpeechTranscriber() = default;

    /** @brief Load the model if it is not yet; the reason it could not, or empty. */
    std::string loadLocked();
    Result decodeLocked(const std::vector<float>& samples, const std::string& prompt,
                        const std::atomic<bool>* abort);

    std::mutex mutex_;
    whisper_context* ctx_ = nullptr;
    std::atomic<bool> loaded_{false};  ///< ctx_ is set, for readers without the lock.
    bool warmed_ = false;              ///< preload() has run its silent decode.
};

}  // namespace magda
