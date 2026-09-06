#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

/**
 * @file InsertCapture.hpp
 * @brief What the hardware said during a live pass, kept for a bounce (#2279).
 *
 * Immutable once taken. Whether it can serve a render is asked once, by
 * InsertCapturePlayback.
 */

namespace magda::engine {

/**
 * @brief The stretch a capture covers.
 *
 * Seconds, since it is written at the device's rate and read at the export's.
 */
struct CaptureWindow {
    double startSeconds = 0.0;
    double endSeconds = 0.0;

    /// Below a sample at any rate: this absorbs double rounding, nothing else.
    static constexpr double kEpsilonSeconds = 1e-9;

    double lengthSeconds() const {
        return std::max(0.0, endSeconds - startSeconds);
    }

    bool covers(const CaptureWindow& other) const {
        return startSeconds <= other.startSeconds + kEpsilonSeconds &&
               endSeconds + kEpsilonSeconds >= other.endSeconds;
    }

    bool operator==(const CaptureWindow&) const = default;
};

/**
 * @brief One short message, at a sample from the window's start.
 *
 * Three data bytes and no more, as clip/MidiEventList.hpp.
 */
struct CapturedMidiEvent {
    std::int64_t sample = 0;

    /// A program change replayed three bytes long is a different message.
    std::uint8_t numBytes = 0;

    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
};

class InsertCapture {
  public:
    /** @brief Filled by InsertCaptureSession and moved in once. */
    struct Contents {
        juce::AudioBuffer<float> audio;
        std::vector<CapturedMidiEvent> midi;
        CaptureWindow window;
        double sampleRate = 0.0;

        /// Seconds, so reading at another rate keeps the delay the pass had.
        double roundTripSeconds = 0.0;

        /// Every sample written and no event dropped. False after a pass that
        /// seeked over the window, and nothing can be built on it.
        bool complete = false;
    };

    InsertCapture() = default;

    explicit InsertCapture(Contents contents) : contents_(std::move(contents)) {}

    const juce::AudioBuffer<float>& audio() const {
        return contents_.audio;
    }

    const std::vector<CapturedMidiEvent>& midi() const {
        return contents_.midi;
    }

    const CaptureWindow& window() const {
        return contents_.window;
    }

    double sampleRate() const {
        return contents_.sampleRate;
    }

    double roundTripSeconds() const {
        return contents_.roundTripSeconds;
    }

    bool complete() const {
        return contents_.complete;
    }

    int numChannels() const {
        return contents_.audio.getNumChannels();
    }

    int lengthInSamples() const {
        return contents_.audio.getNumSamples();
    }

  private:
    Contents contents_;
};

}  // namespace magda::engine
