#pragma once

// Ahead of farbot, which leaves including the standard library to whoever uses
// it: its headers are written to drop into a project that has already done so.
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <farbot/RealtimeObject.hpp>
#include <farbot/fifo.hpp>
#include <memory>
#include <thread>
#include <vector>

#include "exec/RenderContext.hpp"
#include "io/AudioFileReader.hpp"

/**
 * @file PrefetchStream.hpp
 * @brief One file, read ahead of the callback that plays it.
 *
 * The audio thread cannot wait for a disk, so it never touches one: a thread
 * that is allowed to block reads whole chunks ahead of where playback is, and
 * the callback copies out of what is already in memory. What arrives on the
 * audio thread is therefore a pointer swap and a copy, and nothing else.
 *
 * Chunks rather than a ring of samples, because a chunk can be stamped. A seek
 * makes everything already read wrong, and the stamp is how the callback knows
 * that without the reader having to reach into a buffer the callback is using:
 * stale chunks are recognised on the way out and dropped, which is the same
 * epoch discipline the plan swap uses.
 *
 * Threading: read() on the audio thread, fill() on the prefetch thread,
 * everything else before either is running. One stream is one reader and is
 * never shared between tracks.
 */

namespace magda::engine {

/** How much a stream reads ahead, and in what units. */
struct PrefetchSettings {
    /// Samples in one chunk. The unit of disk work: too small and the reader
    /// thread spends its life in syscalls, too large and a seek costs a whole
    /// chunk of latency before anything can play.
    int chunkSamples = 4096;

    /// Chunks in the pool. What is left after subtracting the one the callback
    /// is reading is how far ahead the stream can get, and therefore how long
    /// the reader thread may be held up without the callback noticing.
    int chunkCount = 8;
};

class PrefetchStream {
  public:
    PrefetchStream(std::unique_ptr<AudioFileReader> reader, const RenderContext& context,
                   const PrefetchSettings& settings = {});

    /**
     * @brief Fill @p destination with @p numSamples from @p sourceStart.
     *
     * On the audio thread. Returns how many samples were there to give; the
     * rest of the destination is silent.
     *
     * A start that is not where the last read left off is a seek: what has been
     * read ahead is for somewhere else, so it is dropped and the reader is
     * pointed at the new position. Nothing plays until it has caught up, which
     * is what @ref underruns counts.
     */
    int read(std::int64_t sourceStart, juce::dsp::AudioBlock<float> destination, int numSamples);

    /**
     * @brief Read ahead. On the prefetch thread.
     *
     * Returns true if it did anything, so a caller servicing several streams
     * can tell a busy round from an idle one and sleep on the difference.
     */
    bool fill();

    /// Where the stream will be pointed next. Off the audio thread, before it
    /// is playing: the callback is what moves the cursor once it is.
    void seek(std::int64_t sourceStart);

    /**
     * @brief Blocks the callback could not be given the samples for.
     *
     * A seek costs at least one, because a disk cannot be read inside a
     * callback. Anything beyond that is the reader failing to keep up, which is
     * a property of the machine rather than of the music, and it has to be
     * visible: silence that nobody counted is indistinguishable from a gap in
     * the material.
     *
     * The end of the file is not an underrun. There is nothing late about
     * silence past the last sample.
     */
    int underruns() const {
        return underruns_.load(std::memory_order_relaxed);
    }

    /// Samples in the file. The one thing the audio thread reads from the
    /// reader, and it reads it from a copy taken before anything played.
    std::int64_t lengthInSamples() const {
        return length_;
    }

    double sampleRate() const {
        return sourceSampleRate_;
    }

  private:
    /// One read's worth of audio, and where it came from.
    struct Chunk {
        juce::AudioBuffer<float> audio;
        std::int64_t startSample = 0;
        int numSamples = 0;

        /// Which seek this was read for. A chunk whose generation is not the
        /// one the callback is asking about was read for a position that has
        /// since been abandoned.
        std::uint32_t generation = 0;
    };

    /// Where the reader has been told to be. Written by the callback, read by
    /// the prefetch thread; the pair has to travel together, because a position
    /// read against the wrong generation is a seek to the wrong place.
    struct SeekRequest {
        std::int64_t sourceStart = 0;
        std::uint32_t generation = 0;
    };

    using ChunkFifo = farbot::fifo<Chunk*, farbot::fifo_options::concurrency::single,
                                   farbot::fifo_options::concurrency::single>;

    /// Take the next chunk the callback can use, dropping any that were read
    /// for an abandoned position. Audio thread.
    bool takeNextChunk();

    /// Hand a spent chunk back to the reader. Audio thread.
    void releaseCurrent();

    void requestSeek(std::int64_t sourceStart);

    std::unique_ptr<AudioFileReader> reader_;
    std::int64_t length_ = 0;
    double sourceSampleRate_ = 0.0;
    int numChannels_ = 2;
    int chunkSamples_ = 0;

    std::vector<std::unique_ptr<Chunk>> pool_;
    ChunkFifo filled_;
    ChunkFifo spent_;

    farbot::RealtimeObject<SeekRequest, farbot::RealtimeObjectOptions::realtimeMutatable> request_;

    // Audio thread only.
    Chunk* current_ = nullptr;
    int currentOffset_ = 0;
    std::int64_t nextSample_ = 0;
    std::uint32_t generation_ = 0;

    // Prefetch thread only.
    std::int64_t fillPosition_ = 0;
    std::uint32_t fillGeneration_ = 0;

    /// The reader stopped answering somewhere inside its own file. Nothing to
    /// be done about it here, and everything to be done about not asking it
    /// again every round until something moves.
    bool stalled_ = false;

    std::atomic<int> underruns_{0};
};

}  // namespace magda::engine
