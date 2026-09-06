#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

/**
 * @file InsertCapture.hpp
 * @brief What the hardware said, kept so a bounce can play it back (#2279).
 *
 * A render pulls an hour of timeline in a minute and no outboard answers at
 * that speed, so a bounce plays back a recording of a live pass instead. This
 * is the recording: the audio and MIDI that came back, the rate it was taken
 * at, the window it covers, and the round trip measured with it.
 *
 * Immutable once taken. A capture that could still be written to is one every
 * reader has to defend against, and the only question anyone asks of it -- can
 * this serve that render? -- is answered once, by InsertCapturePlayback.
 */

namespace magda::engine {

/**
 * @brief The stretch of timeline a capture covers, in seconds.
 *
 * Seconds rather than samples because a capture is written at whatever rate the
 * device was open at and read at whatever rate the export asked for, and a
 * position counted in samples means a different instant on each side.
 */
struct CaptureWindow {
    double startSeconds = 0.0;
    double endSeconds = 0.0;

    /**
     * @brief Below any sample at any rate a device runs at.
     *
     * So it absorbs the rounding in the seconds arithmetic and nothing else.
     */
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
 * @brief One short message, at one sample from the window's start.
 *
 * Three data bytes and no more, the bound the clip events already carry
 * (clip/MidiEventList.hpp): what a capture costs stays counted in events rather
 * than in bytes, and the write path can size its room up front.
 */
struct CapturedMidiEvent {
    std::int64_t sample = 0;

    /**
     * @brief One, two or three.
     *
     * A hardware return speaks clock and program change as well as notes, and a
     * message replayed a byte longer than it was sent is a different message.
     */
    std::uint8_t numBytes = 0;

    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
};

class InsertCapture {
  public:
    /// Everything a capture is, moved in once. InsertCaptureSession fills it.
    struct Contents {
        juce::AudioBuffer<float> audio;
        std::vector<CapturedMidiEvent> midi;
        CaptureWindow window;
        double sampleRate = 0.0;

        /**
         * @brief The live insert's round trip, in seconds.
         *
         * Seconds so that reading the capture at another rate keeps the delay
         * the pass actually had.
         */
        double roundTripSeconds = 0.0;

        /**
         * @brief Every sample of the window was written and no event dropped.
         *
         * A pass that seeked over part of the window leaves this false, and
         * nothing can be built on it.
         */
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
