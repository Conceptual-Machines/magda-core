#pragma once

// farbot leaves including the standard library to whoever uses it: its headers
// are written to drop into a project that has already done so, and on their own
// they do not compile. The JUCE headers above it here bring in what it needs.
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

    /**
     * @brief Point the reader before anything is reading it at all.
     *
     * Before the stream is registered with a prefetch thread and before any
     * block has read from it, on the thread that made it. Not a seek and not a
     * cue: it moves both cursors while there is nobody to disagree with, so
     * there is no generation to bump, nothing in flight to throw away, and no
     * block to wait for.
     *
     * What it is for is that a stream is made for a clip, and a clip starts
     * somewhere. Without this a new stream spends its first reads on the
     * material at sample zero, which is not what it was opened for, and the
     * cue that redirects it cannot be taken up until a callback has run
     * (@ref applyPendingCue). Every one of those reads would then be thrown
     * away, and the reader would start again from behind.
     */
    void startAt(std::int64_t sourceStart);

    /**
     * @brief Point the reader at a position before anything asks for it.
     *
     * From any thread that is not the audio thread, at any time: whoever
     * schedules material knows where playback will be before the callback does,
     * and a stream told in advance is one that does not have to seek in the
     * block that needs the samples.
     *
     * Nothing here touches what the callback owns. It publishes a cue, and the
     * callback picks it up at the top of its next block through
     * @ref applyPendingCue, so the seek itself happens where every other seek
     * happens. That is a block's delay before the reader starts moving, which
     * is nothing against the reason to cue at all: a clip scheduled a moment
     * ahead is read ahead long before it plays.
     *
     * A cue points a stream that is not sounding. One that is keeps playing and
     * takes the cue up when it stops, because a single reader cannot be in two
     * places: pointing it somewhere else would abandon the material still
     * playing and hand back the pool holding it, for a position the next read
     * would immediately override.
     *
     * That is also why a loop's return is not seamless here. Reading the top of
     * a loop while the end of it is still playing means holding two positions
     * at once, which is a second fill cursor and a second pool, and it belongs
     * with whoever owns clip looping (#1890) rather than being half-built
     * underneath them. Today a wrap costs the block it happens in, and the
     * underrun count says so.
     */
    void seek(std::int64_t sourceStart);

    /**
     * @brief Take up a cue, if one is waiting. On the audio thread.
     *
     * Once per block, at the top of it, by whoever drives the stream. Not done
     * inside read(), and the difference matters: what decides whether a cue is
     * taken up now or waits is whether this stream is sounding, and that
     * question is only answerable at a block boundary. Asked again halfway
     * through, a stream that simply has not reached its read yet looks exactly
     * like one that is silent.
     *
     * It has to be a separate call for the same reason. The stream a cue is
     * most useful to is one nobody is reading from: a clip that has not started
     * would otherwise not hear about it until the material was already due.
     */
    void applyPendingCue();

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

    /// A position published from off the audio thread, waiting to be taken up.
    /// The other direction from @ref request_, and a separate object for the
    /// same reason it is a different type: one writer each, so neither side is
    /// ever writing what the other is reading.
    farbot::RealtimeObject<SeekRequest, farbot::RealtimeObjectOptions::nonRealtimeMutatable> cue_;

    /// The cueing side's own counter. Not shared with @ref generation_, which
    /// the audio thread owns: what makes a cue new is that it is not the one
    /// already taken up.
    std::uint32_t cueGeneration_ = 0;

    // Audio thread only.
    Chunk* current_ = nullptr;
    int currentOffset_ = 0;
    std::int64_t nextSample_ = 0;
    std::uint32_t generation_ = 0;
    std::uint32_t appliedCue_ = 0;

    /// Whether anything could have played out of this stream since the last
    /// cue check. What separates a stream waiting to be given somewhere to go
    /// from one already on its way somewhere, which is not the same question as
    /// whether it was asked: a clip placed past the end of its own material is
    /// asked every block and plays nothing.
    bool readSinceCueCheck_ = false;

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
