// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "io/LiveInput.hpp"
#include "transport/TimeDomains.hpp"

/**
 * @file CaptureRing.hpp
 * @brief The master's recent audio, kept so it can be taken afterwards (#2311).
 *
 * The SampleRing contract, longer and in stereo: the writer never waits and
 * overwrites what nobody took. It also notes where the latest transport pass
 * and the latest sound heard while stopped lie, which is what KEEP takes.
 */

namespace magda::engine {

class TempoMap;

/** @brief The bar line @p bars bars back from @p endBeat, the bar it falls in counting as one. */
double barsBefore(const TempoMap& map, double endBeat, int bars);

/** @brief Frames of the ring, half open; empty before there is one. */
struct CaptureSpan {
    std::int64_t start = -1;
    std::int64_t end = -1;

    bool empty() const {
        return end <= start;
    }
};

/** @brief One live MIDI message as the ring keeps it, on the audio's frame clock. */
struct CaptureMidiEvent {
    std::int64_t frame = 0;
    LiveMidiSourceId source = kAnyLiveMidiSource;
    std::uint8_t size = 0;
    std::array<std::uint8_t, 3> bytes{};
};

/** @brief Where the latest pass and phrase lie, before clamping to what is still kept. */
struct CaptureMarks {
    CaptureSpan pass;
    BeatRange passBeats;
    CaptureSpan phrase;
};

/** @brief The ring as the meter and KEEP read it: cheap enough to poll. */
struct CaptureMeter {
    std::int64_t writtenFrames = 0;
    std::int64_t capacityFrames = 0;
    std::int64_t oldestReadable = 0;

    /// The latest stretch the transport played without a stop, wrap or locate.
    CaptureSpan pass;
    BeatRange passBeats;

    /// The latest sound heard with the transport stopped, back to a silence or a stop.
    CaptureSpan phrase;

    std::vector<float> peaks;  ///< the whole ring, oldest first
};

/** @brief @p marks into @p meter, clamped to the frames it says are readable. */
void applyMarks(CaptureMeter& meter, const CaptureMarks& marks);

class CaptureRing {
  public:
    /// Frames per coarse peak: five minutes at 48 kHz is about 14000 of them.
    static constexpr std::int64_t kFramesPerPeak = 1024;

    CaptureRing(int numChannels, std::int64_t capacityFrames, double sampleRate);

    int numChannels() const {
        return numChannels_;
    }
    std::int64_t capacityFrames() const {
        return capacity_;
    }
    std::int64_t writtenFrames() const {
        return written_.load(std::memory_order_acquire);
    }

    /// The oldest frame read() can still copy. Any thread.
    std::int64_t oldestReadable() const;

    /// The latest pass and phrase, unclamped. Any thread.
    CaptureMarks marks() const;

    /// Audio thread, once per rendered stretch. A @p block of one channel fills both sides.
    void write(const juce::AudioBuffer<float>& block, BeatRange beats, bool playing);

    /// Audio thread, just before write(): the live MIDI inside the same stretch, which
    /// starts at @p segmentStart of the callback's @p streams and runs @p numSamples.
    void writeMidi(std::span<const LiveMidiStream> streams, int segmentStart, int numSamples);

    /// The MIDI still in the ring from @p firstFrame on, oldest first. Any thread.
    std::vector<CaptureMidiEvent> midiFrom(std::int64_t firstFrame) const;

    /// Copy @p frames frames from @p firstFrame into @p out; false if any was overwritten.
    bool read(std::int64_t firstFrame, std::int64_t frames, juce::AudioBuffer<float>& out) const;

    /// The ring in @p slices peaks, and where its pass and phrase lie. Any thread.
    CaptureMeter meter(int slices) const;

    /// Frames @p first to @p end in @p slices peaks, oldest first: from the coarse chunk
    /// peaks when a slice spans several, from the audio itself when zoomed in closer.
    std::vector<float> peaks(std::int64_t first, std::int64_t end, int slices) const;

  private:
    /// The oldest frame still safe to read while @p margin more are being written.
    std::int64_t oldestKept(std::int64_t written, std::int64_t margin = 0) const;

    void notePhrase(std::int64_t chunkStart, std::int64_t chunkEnd);

    int numChannels_;
    std::int64_t capacity_;
    std::int64_t gapFrames_;
    std::vector<std::unique_ptr<std::atomic<float>[]>> channels_;
    std::atomic<std::int64_t> written_{0};

    // A coarse peak per chunk of frames, for the meter and for finding phrases.
    std::unique_ptr<std::atomic<float>[]> peaks_;
    std::int64_t peakSlots_;

    std::atomic<std::int64_t> passStart_{-1};
    std::atomic<std::int64_t> passEnd_{-1};
    std::atomic<double> passStartBeat_{0.0};
    std::atomic<double> passEndBeat_{0.0};
    std::atomic<std::int64_t> phraseStart_{-1};
    std::atomic<std::int64_t> phraseEnd_{-1};

    // Live MIDI, one packed word per message beside its frame.
    std::unique_ptr<std::atomic<std::int64_t>[]> midiFrames_;
    std::unique_ptr<std::atomic<std::uint64_t>[]> midiWords_;
    std::int64_t midiCapacity_;
    std::atomic<std::int64_t> midiCount_{0};

    // Audio thread only.
    float chunkPeak_ = 0.0f;
    bool lastPlaying_ = false;
    double lastBeatEnd_ = 0.0;
    std::int64_t stoppedAt_ = 0;
    bool awaitingSilence_ = false;
};

}  // namespace magda::engine
