#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cstdint>
#include <farbot/RealtimeObject.hpp>
#include <memory>
#include <vector>

#include "core/ClipInfo.hpp"
#include "exec/RenderContext.hpp"
#include "io/LiveInput.hpp"
#include "io/RecordStream.hpp"
#include "io/TakeFileSink.hpp"
#include "transport/TransportState.hpp"

/**
 * @file TakeRecorder.hpp
 * @brief An arrangement audio take, from armed to clip (#2461).
 *
 * One track's recording: the input slice 1 delivers, through the queue slice 2
 * drains, into the files a clip is made of. The rules it keeps are the model's
 * rather than any engine's, which is why they live here:
 *
 * - A take holds what happened, so the head is corrected by the input's
 *   declared latency rather than the clip being moved to meet it.
 * - Loop recording is loop-aligned: one file per pass, all sharing the clip
 *   start, and a pass that cannot share it is not a take (AudioTake has no
 *   offset of its own).
 * - The active take is the last full pass, since only the last one can be cut
 *   off mid-way.
 *
 * Nothing here makes a model object on the audio thread and no clip exists
 * until the recording ends: @ref TakeRecorder::finish is where a take becomes
 * one, on the thread that asked for it.
 */

namespace magda::engine {

/** @brief What a finished take became, and what it cost. */
struct RecordedTake {
    /// Where the clip goes, and how much of the timeline it covers.
    double startBeat = 0.0;
    double lengthBeats = 0.0;

    /// What the clip plays. Empty for a take that captured nothing.
    juce::File file;

    /**
     * @brief The passes and the one that plays.
     *
     * `takes` is empty for a single pass, which is what an ordinary clip is;
     * `events` is empty either way, because an event names a SourceId and the
     * pool that hands those out is the host's.
     */
    AudioClipModel clip;

    /// Samples the disk could not keep up with. Above zero is a hole in the
    /// performance, and the boundaries between passes moved by as much.
    std::int64_t samplesLost = 0;

    /// Whether a file refused a write or could not be opened at all.
    bool failed = false;

    bool empty() const {
        return file == juce::File();
    }
};

/** @brief What a take reads, and where it writes. */
struct TakeRecorderSettings {
    /// Channels of the device's input the take holds, in file order: one for a
    /// mono input, two for a stereo pair. Never widened -- what a stereo track
    /// makes of a mono input is playback's business, not the file's.
    std::vector<int> channels;

    /**
     * @brief The input's declared latency, in samples (#2459).
     *
     * What arrives has already happened, so the sample under the playhead at
     * record start arrives this much later: a positive latency drops that many
     * from the head of the take. A negative one is an adjustment pulling the
     * other way, and pads the head with that much silence instead.
     */
    int latencySamples = 0;

    /// The project's recordings directory, which the host names.
    juce::File directory;

    /// What the take's files are called, before the pass number.
    juce::String name = "take";

    AudioFileSpec file;

    /// How much the queue holds. Its channel count is the take's.
    RecordStreamSettings stream;
};

/**
 * @brief One take: fed on the audio thread, closed on the thread that stops it.
 *
 * Made before recording starts, because nothing about a file can happen on the
 * audio thread. It captures from the first block the transport is rolling and
 * not counting in, and stops at the first block it is not: a take is closed by
 * a stop, and a second play is a second take.
 */
class TakeRecorder {
  public:
    TakeRecorder(const LiveInputFeed& feed, const RenderContext& context,
                 TakeRecorderSettings settings);

    ~TakeRecorder() = default;

    /// Neither copied nor moved: the queue holds a reference to the sink.
    TakeRecorder(const TakeRecorder&) = delete;
    TakeRecorder& operator=(const TakeRecorder&) = delete;
    TakeRecorder(TakeRecorder&&) = delete;
    TakeRecorder& operator=(TakeRecorder&&) = delete;

    /// The queue the record thread drains. Registered by whoever owns the
    /// thread, and unregistered before @ref finish.
    RecordStream& stream() {
        return stream_;
    }

    /**
     * @brief Take what this block carried. On the audio thread, once a block.
     *
     * @p loop is the transport's, and is what tells a loop wrap from a locate:
     * a wrap opens the next pass, and anything else ends the take, since
     * material after a jump belongs where the cursor went.
     */
    void capture(const BlockInfo& block, bool countingIn, const LoopRange& loop);

    /// Samples offered to the queue so far. Read from any thread; what a
    /// running take is drawn from (#2463).
    std::int64_t capturedSamples() const {
        return captured_.load(std::memory_order_relaxed);
    }

    /// Whether the take is still taking blocks. Read from any thread.
    bool rolling() const {
        return rolling_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Close the take and say what it became.
     *
     * Off the audio thread, after the callback can no longer reach this: it
     * drains what is queued, closes the last file and reads the passes back.
     * Idempotent, and a take that never rolled comes back empty.
     */
    RecordedTake finish();

  private:
    enum class State : std::uint8_t { waiting, rolling, stopped };

    /// The first block of the take: where it starts, and the head correction.
    void start(const BlockInfo& block, const LoopRange& loop);

    /// A wrap: the pass ends a latency later, since that is when the samples
    /// the wrap belongs to arrive.
    void openPass(const LoopRange& loop);

    void stop();

    /// This block's input, into the queue, split at a pass end if one falls
    /// inside it.
    void write(const BlockInfo& block);

    /// Where the take is, once there is nothing more to add to it.
    void placeClip(RecordedTake& take) const;

    TakeRecorderSettings settings_;

    LiveAudioInput input_;
    TakeFileSink sink_;
    RecordStream stream_;

    /// The block, narrowed to the channels the take holds.
    juce::AudioBuffer<float> scratch_;

    /// The silence a negative latency puts at the head, sized once.
    juce::AudioBuffer<float> pad_;

    State state_ = State::waiting;

    /// Arrivals still to drop before the take's first sample.
    int headDrop_ = 0;

    /// Samples still to write before the current pass ends, or -1 for none.
    int pendingSplit_ = -1;

    std::int64_t written_ = 0;

    double startBeat_ = 0.0;
    double lastBeat_ = 0.0;
    double latencySeconds_ = 0.0;

    /// Whether the take began on the loop start, which is what says its first
    /// pass is a pass rather than a lead-in.
    bool startedAtLoopStart_ = false;

    /// The loop a wrap was seen against, and whether one ever was.
    bool wrapped_ = false;
    double loopStartBeat_ = 0.0;
    double loopEndBeat_ = 0.0;

    bool finished_ = false;
    RecordedTake result_;

    std::atomic<std::int64_t> captured_{0};
    std::atomic<bool> rolling_{false};
};

/// The takes a callback feeds. Not owned: whoever publishes them keeps them
/// alive until it has published something that does not name them.
using RecordingTakes = std::vector<TakeRecorder*>;

/**
 * @brief What the audio thread records into, replaced on the publishing thread.
 *
 * A feed rather than part of a plan, for the reason the clips are one: arming a
 * track recompiles a plan, but starting a recording is not a structural edit
 * and must not cost one. Publishing waits for the block the callback is in, so
 * a take taken out of the set is one nothing is writing to by the time the call
 * returns, which is what makes it safe to finish.
 */
class RecordingFeed {
    using Published = farbot::RealtimeObject<std::shared_ptr<const RecordingTakes>,
                                             farbot::RealtimeObjectOptions::nonRealtimeMutatable>;

  public:
    /// On the publishing thread.
    void publish(std::shared_ptr<const RecordingTakes> takes) {
        published_.nonRealtimeReplace(std::move(takes));
    }

    /// What is live, for as long as this exists. On the audio thread. Null
    /// until something is published, which is a session recording nothing.
    class Reader {
      public:
        explicit Reader(RecordingFeed& feed) : access_(feed.published_) {}

        const RecordingTakes* get() const noexcept {
            return (*access_).get();
        }
        explicit operator bool() const noexcept {
            return get() != nullptr;
        }

      private:
        Published::ScopedAccess<farbot::ThreadType::realtime> access_;
    };

  private:
    Published published_;
};

}  // namespace magda::engine
