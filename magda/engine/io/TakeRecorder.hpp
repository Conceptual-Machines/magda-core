#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cstdint>
#include <vector>

#include "core/ClipInfo.hpp"
#include "exec/RenderContext.hpp"
#include "io/LiveInput.hpp"
#include "io/RecordStream.hpp"
#include "io/RecordingFeed.hpp"
#include "io/TakeFileSink.hpp"
#include "io/TakePasses.hpp"
#include "tap/RecordTap.hpp"
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
 * Three positions are kept apart on purpose, because latency, tempo and a disk
 * that falls behind separate them:
 *
 * - when a sample arrived, which is what the callback counts;
 * - where it belongs, which is `arrival - latency` and the take's own index;
 * - how much of it reached a file, which is the writer's and nobody else's.
 *
 * The contract with TakeFileSink follows from that. A pass end is named as a
 * take index, by the wrap that ended the pass, and never behind what has already
 * been queued. So the writer splits on a position it cannot have passed, and a
 * pass holds the same samples however often the disk is drained.
 *
 * A boundary is never named for a wrap that has not happened. It could be:
 * a negative adjustment queues a pass's last samples before the transport
 * reaches the loop end, so the end is predictable a block or two ahead. But a
 * prediction can be falsified -- the loop can be switched off in between -- and
 * a boundary handed over cannot be withdrawn, which would cut continuous audio
 * into takes of a loop that never came round. The clamp above is what is paid
 * instead: with a negative adjustment the first pass runs long by it, and the
 * passes after it sit that much late against the loop.
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

    /// Pass ends the write path could not be told about, which is two loop
    /// passes run together in one file. Above zero and `clip.takes` is not one
    /// pass each.
    std::int64_t passesLost = 0;

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

    /// How much of the pass in flight is published (#2463).
    RecordTapSettings tap;
};

/**
 * @brief One take: fed on the audio thread, closed on the thread that stops it.
 *
 * Made before recording starts, because nothing about a file can happen on the
 * audio thread. It captures from the first block the transport is rolling and
 * not counting in, and stops at the first block it is not: a take is closed by
 * a stop, and a second play is a second take.
 */
class TakeRecorder final : public TakeCapture {
  public:
    TakeRecorder(const LiveInputFeed& feed, const RenderContext& context,
                 TakeRecorderSettings settings);

    ~TakeRecorder() override = default;

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

    void capture(const BlockInfo& block, bool countingIn, const LoopRange& loop) override;

    /// Where the pass in flight is published (#2463).
    const RecordTap& tap() const override {
        return tap_;
    }

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

    /// A wrap: where the pass ended, handed to the sink. The one place a
    /// boundary is named, so the rule above it holds everywhere.
    void openPass(const BlockInfo& block, const LoopRange& loop);

    void stop();

    /// This block's input, into the queue. Where a pass ends inside it is the
    /// sink's to act on, since only the sink knows what reached the disk.
    void write(const BlockInfo& block);

    /// Where the take is, once there is nothing more to add to it.
    void placeClip(RecordedTake& take) const;

    TakeRecorderSettings settings_;

    LiveAudioInput input_;
    TakeFileSink sink_;
    RecordStream stream_;
    RecordTap tap_;

    /// The block, narrowed to the channels the take holds.
    juce::AudioBuffer<float> scratch_;

    /// The silence a negative latency puts at the head, sized once.
    juce::AudioBuffer<float> pad_;

    State state_ = State::waiting;

    /// Arrivals still to drop before the take's first sample.
    int headDrop_ = 0;

    /// Samples written to the queue, and the timeline the take has covered.
    /// The second is the first read a latency earlier, which is why a pass
    /// boundary is counted in it.
    std::int64_t written_ = 0;
    std::int64_t arrivals_ = 0;

    /// The first pass end asked for, or -1. What says whether any pass ever
    /// began where the loop does.
    std::int64_t firstBoundary_ = -1;

    std::int64_t boundariesLost_ = 0;

    /// Where the take begins, and where its audio reaches. The end is asked of
    /// the tempo map rather than accumulated, so a tempo change inside a take
    /// moves it by what actually happened.
    double startBeat_ = 0.0;
    double startSeconds_ = 0.0;
    double endBeat_ = 0.0;

    /// Where the pass in flight began, in samples written and in the beat they
    /// reach. The tap counts a pass from what was written rather than from the
    /// boundary the sink was given, so its peaks and its length are the same
    /// stretch of audio.
    std::int64_t passOrigin_ = 0;
    double passOriginBeat_ = 0.0;

    /// Whether the take began on the loop start, which is what says its first
    /// pass is a pass rather than a lead-in.
    bool startedAtLoopStart_ = false;

    /// The loop the last wrap was seen against.
    double loopStartBeat_ = 0.0;
    double loopEndBeat_ = 0.0;

    bool finished_ = false;
    RecordedTake result_;

    std::atomic<std::int64_t> captured_{0};
    std::atomic<bool> rolling_{false};
};

}  // namespace magda::engine
