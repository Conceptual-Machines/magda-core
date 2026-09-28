#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

/**
 * @file RecordStream.hpp
 * @brief What the audio thread hands a disk during a take (#2460).
 *
 * The mirror of PrefetchStream: one reads ahead of the callback, this one
 * writes behind it. The callback copies into memory and returns; a thread
 * allowed to block moves it to disk.
 *
 * Not tap/SampleRing.hpp, whose contract is the opposite one. A ring lets the
 * writer overwrite what the reader has not taken, which is right for a meter
 * and for a retro-capture buffer (#2311) where the present matters more than
 * the past. A take is the past: a sample the disk did not keep up with is a
 * hole in a performance, so this refuses to overwrite and says how much it
 * had to drop instead.
 */

namespace magda::engine {

/**
 * @brief One short message, at a sample from the take's start.
 *
 * Three data bytes and no more, as clip/MidiEventList.hpp and
 * insert/InsertCapture.hpp: what a MIDI take holds is notes, CC and pitch
 * bend, so a longer message has nowhere to go and is dropped and counted
 * rather than truncated into a different message.
 */
struct RecordedMidiEvent {
    std::int64_t sample = 0;

    /// A program change kept three bytes long is a different message.
    std::uint8_t numBytes = 0;

    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
};

/**
 * @brief Where a drained take goes.
 *
 * Called on the record thread and nowhere else, which is what makes a file
 * safe to touch here. Returning false means the write failed and the take is
 * broken: the stream stops offering it samples and says so (#2461 decides
 * what a broken take becomes).
 */
class RecordSink {
  public:
    virtual ~RecordSink() = default;

    virtual bool writeAudio(juce::dsp::AudioBlock<const float> audio, int numSamples) {
        juce::ignoreUnused(audio, numSamples);
        return true;
    }

    virtual bool writeMidi(std::span<const RecordedMidiEvent> events) {
        juce::ignoreUnused(events);
        return true;
    }

    /// After the last sample, on the thread that called RecordStream::finish.
    virtual void finish() {}
};

/** How much a stream holds, and how much of it one round of draining moves. */
struct RecordStreamSettings {
    /// Zero for a MIDI-only take.
    int numChannels = 2;

    /**
     * @brief Samples of room, per channel. Rounded up to a power of two.
     *
     * How long the disk may stall before a take loses samples. The default is
     * about a second and a half at 44.1 kHz, which is far longer than a write
     * that is merely slow and short enough that a stream costs kilobytes
     * rather than megabytes per armed track.
     */
    int capacitySamples = 65536;

    int midiCapacityEvents = 1024;

    /// Samples one drain hands the sink. The unit of disk work, as
    /// PrefetchSettings::chunkSamples is the unit of reading.
    int chunkSamples = 4096;
};

/**
 * @brief One take's queue: written on the audio thread, drained on the record
 *        thread.
 *
 * Single producer, single consumer, and neither side allocates or locks. The
 * room is taken at construction, which is what makes a write a copy and
 * nothing else.
 */
class RecordStream {
  public:
    explicit RecordStream(RecordSink& sink, const RecordStreamSettings& settings = {});

    RecordStream(const RecordStream&) = delete;
    RecordStream& operator=(const RecordStream&) = delete;

    /**
     * @brief Append @p numSamples of @p audio. On the audio thread.
     *
     * False when there was no room: the whole block is dropped and counted.
     * Dropped whole rather than in part, because half a block leaves a hole
     * in the middle of a file that nothing downstream can see; a count of
     * lost samples is a fact the finished take can carry.
     */
    bool writeAudio(juce::dsp::AudioBlock<const float> audio, int numSamples);

    /**
     * @brief Append @p midi, stamped from @p blockStartSample. On the audio
     *        thread.
     *
     * The position is the caller's because a MIDI-only take has no audio to
     * count samples with, and because both must be stamped against the take's
     * clock rather than each against its own.
     *
     * False when anything was dropped: no room, or a message longer than
     * three bytes.
     */
    bool writeMidi(const juce::MidiBuffer& midi, std::int64_t blockStartSample);

    /**
     * @brief Move one chunk to the sink. On the record thread.
     *
     * Returns whether it did anything, so a thread servicing several streams
     * can tell a busy round from an idle one. A failed write drops the chunk
     * rather than holding it: the take is already broken, and a queue nobody
     * drains would stop the audio thread writing the next one.
     */
    bool drain();

    /**
     * @brief Drain what is left and close the sink.
     *
     * After the audio thread has stopped writing, on a thread that may block.
     * Draining and closing are one call because a sink closed with samples
     * still queued is a take that ends early with nothing to say about it.
     */
    void finish();

    /// Samples the disk could not keep up with. Read from any thread; a take
    /// reporting more than zero has a hole in it.
    std::int64_t samplesLost() const {
        return samplesLost_.load(std::memory_order_relaxed);
    }

    /// Events dropped for want of room, or for being longer than three bytes.
    std::int64_t eventsLost() const {
        return eventsLost_.load(std::memory_order_relaxed);
    }

    /// Whether a write to the sink failed. Read from any thread.
    bool failed() const {
        return failed_.load(std::memory_order_relaxed);
    }

    /// Samples of room per channel, after rounding.
    int capacitySamples() const {
        return capacity_;
    }

    int numChannels() const {
        return audio_.getNumChannels();
    }

  private:
    int contiguousAudioToDrain(std::uint64_t read, std::uint64_t available) const;

    RecordSink& sink_;

    juce::AudioBuffer<float> audio_;
    int capacity_ = 0;
    std::uint64_t mask_ = 0;
    int chunkSamples_ = 0;

    std::vector<RecordedMidiEvent> midi_;
    std::uint64_t midiMask_ = 0;

    std::atomic<std::uint64_t> audioWrite_{0};
    std::atomic<std::uint64_t> audioRead_{0};
    std::atomic<std::uint64_t> midiWrite_{0};
    std::atomic<std::uint64_t> midiRead_{0};

    std::atomic<std::int64_t> samplesLost_{0};
    std::atomic<std::int64_t> eventsLost_{0};
    std::atomic<bool> failed_{false};
};

}  // namespace magda::engine
