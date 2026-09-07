#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

#include "core/ClipInfo.hpp"
#include "exec/RenderContext.hpp"
#include "io/LiveInput.hpp"
#include "io/RecordStream.hpp"
#include "io/RecordingFeed.hpp"
#include "io/TakePasses.hpp"
#include "transport/TempoMap.hpp"
#include "transport/TimeDomains.hpp"
#include "transport/TransportState.hpp"

/**
 * @file MidiTakeRecorder.hpp
 * @brief A MIDI take, from armed to clip (#2462).
 *
 * The same pass as an audio take and the other kind of material: the live MIDI
 * slice 1 delivers, through the queue slice 2 drains, into the notes a clip is
 * made of. The loop rules are shared with audio (io/TakePasses.hpp) because
 * they are the model's rather than either kind's.
 *
 * What differs is what a position costs. An audio take is committed to a file
 * as it goes, so a pass boundary can never be named behind what is already
 * queued and a negative adjustment lengthens the first pass. Nothing here is
 * committed until @ref MidiTakeRecorder::finish, so a boundary is named where
 * the wrap is and events fall either side of it by their own stamps: a
 * negative adjustment moves an event across a boundary rather than moving the
 * boundary.
 *
 * Every event is stamped in the take's own positions on the way into the queue
 * -- `arrival - latency`, the one head correction, applied once -- so nothing
 * downstream has to know what the input's latency was. An event landing before
 * the take started is one that arrived before the playhead reached it, and is
 * dropped.
 *
 * Beats are the domain, and the conversion is the last step rather than the
 * first: the audio thread stamps samples, and @ref MidiTakeRecorder::finish
 * asks the tempo map where each one falls. A pass recorded across a tempo
 * change therefore lands where it was played.
 */

namespace magda::engine {

/** @brief What a finished MIDI take became, and what it cost. */
struct RecordedMidiTake {
    /// Where the clip goes, and how much of the timeline it covers.
    double startBeat = 0.0;
    double lengthBeats = 0.0;

    /// What the clip plays: the active take's events, which the model holds on
    /// the clip itself rather than reaching into `clip.takes` for.
    MidiTake active;

    /// The passes and the one that plays. `takes` is empty for a single pass,
    /// which is what an ordinary clip is.
    MidiClipModel clip;

    /// Whether the take ever rolled. A recording that captured no events is
    /// still a take, because an empty MIDI clip is a legitimate result.
    bool recorded = false;

    /// Events the queue had no room for, or that were longer than the three
    /// bytes it carries. Above zero is a hole in the performance.
    std::int64_t eventsLost = 0;

    /// Events that arrived whole and have no field in the model: program
    /// change, aftertouch, channel pressure, anything from the system. Dropped
    /// here, where it can be said, rather than inside a converter.
    std::int64_t messagesDropped = 0;

    /// Pass ends that did not fit, which is two loop passes run together in
    /// one take. Above zero and `clip.takes` is not one pass each.
    std::int64_t passesLost = 0;

    bool empty() const {
        return !recorded;
    }
};

/** @brief What a MIDI take reads, and how much it can hold. */
struct MidiTakeRecorderSettings {
    /// Which input the take listens to, or kAnyLiveMidiSource for all of them.
    LiveMidiSourceId source = kAnyLiveMidiSource;

    /**
     * @brief The input's declared latency, in samples (#2459).
     *
     * What arrived has already happened, so an event's place on the timeline is
     * `arrival - latency`: a positive latency drops what arrived before the
     * take started, and a negative one moves everything that much later.
     */
    int latencySamples = 0;

    /// How much the queue holds. Its channel count is always zero: a MIDI take
    /// has no audio to carry.
    RecordStreamSettings stream;
};

/** @brief The record thread's end of a MIDI take: the events, in arrival order. */
class MidiTakeSink final : public RecordSink {
  public:
    bool writeMidi(std::span<const RecordedMidiEvent> events) override;

    /// After RecordStream::finish, off the record thread.
    std::span<const RecordedMidiEvent> events() const {
        return events_;
    }

  private:
    std::vector<RecordedMidiEvent> events_;
};

/**
 * @brief One MIDI take: fed on the audio thread, closed on the thread that
 *        stops it.
 *
 * It captures from the first block the transport is rolling and not counting
 * in, and stops at the first block it is not: a take is closed by a stop, and a
 * second play is a second take.
 */
class MidiTakeRecorder final : public TakeCapture {
  public:
    MidiTakeRecorder(const LiveInputFeed& feed, MidiTakeRecorderSettings settings);

    ~MidiTakeRecorder() override = default;

    /// Neither copied nor moved: the queue holds a reference to the sink.
    MidiTakeRecorder(const MidiTakeRecorder&) = delete;
    MidiTakeRecorder& operator=(const MidiTakeRecorder&) = delete;
    MidiTakeRecorder(MidiTakeRecorder&&) = delete;
    MidiTakeRecorder& operator=(MidiTakeRecorder&&) = delete;

    /// The queue the record thread drains. Registered by whoever owns the
    /// thread, and unregistered before @ref finish.
    RecordStream& stream() {
        return stream_;
    }

    void capture(const BlockInfo& block, bool countingIn, const LoopRange& loop) override;

    /// Events offered to the queue so far. Read from any thread; what a running
    /// take is drawn from (#2463).
    std::int64_t capturedEvents() const {
        return captured_.load(std::memory_order_relaxed);
    }

    /// Whether the take is still taking blocks. Read from any thread.
    bool rolling() const {
        return rolling_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Close the take and say what it became.
     *
     * Off the audio thread, after the callback can no longer reach this. @p
     * tempo is asked for every event's beat, which is why it is a parameter
     * here where the audio take needs none: an audio take converts one
     * position as it goes, and this one has nothing to convert until the queue
     * has given the events back.
     *
     * Idempotent, and a take that never rolled comes back empty.
     */
    RecordedMidiTake finish(const TempoMap& tempo);

  private:
    enum class State : std::uint8_t { waiting, rolling, stopped };

    /// Pass ends one take can hold. As TakeFileSink's own lane: what will not
    /// fit is refused rather than dropped, so a take can say its passes ran
    /// together.
    static constexpr std::size_t kMaxPasses = 256;

    void start(const BlockInfo& block, const LoopRange& loop);

    /// A wrap: where the pass ended, in the take's own positions.
    void openPass(const LoopRange& loop);

    void stop();

    /// This block's events, stamped and queued.
    void write(const BlockInfo& block);

    /// The beat @p moment falls on, as BlockInfo::beatAtTime reads it.
    double beatAt(const TempoMap& tempo, double moment) const;

    /// The take position @p sample falls on, in seconds.
    double timeAt(std::int64_t sample) const;

    /// The events, split into the passes @p edges names and put into beats.
    std::vector<MidiTake> passesFrom(const TempoMap& tempo, std::span<const std::int64_t> edges);

    /// Where the take is, once there is nothing more to add to it.
    void placeClip(const TempoMap& tempo, RecordedMidiTake& take) const;

    MidiTakeRecorderSettings settings_;

    LiveMidiInput input_;
    MidiTakeSink sink_;
    RecordStream stream_;

    /// This block's events, sized once so a capture cannot allocate.
    juce::MidiBuffer events_;

    State state_ = State::waiting;

    /// The timeline the take has covered, counted in arrivals, and the take
    /// position its last block reaches. The second is the first corrected by
    /// the latency, which is the domain every stamp and boundary is in.
    std::int64_t arrivals_ = 0;
    std::int64_t end_ = 0;

    std::array<std::int64_t, kMaxPasses> boundaries_{};
    std::size_t numBoundaries_ = 0;
    std::int64_t boundariesLost_ = 0;

    double startBeat_ = 0.0;
    double startSeconds_ = 0.0;
    double sampleRate_ = 0.0;

    /// The block's own beat origin, so a beat is read here exactly as the
    /// render read it.
    MaterialOrigin origin_;

    /// Whether the take ever rolled, which a stop cannot be read back from.
    bool rolled_ = false;

    /// Whether the take began on the loop start, which is what says its first
    /// pass is a pass rather than a lead-in.
    bool startedAtLoopStart_ = false;

    /// The loop the last wrap was seen against.
    double loopStartBeat_ = 0.0;
    double loopEndBeat_ = 0.0;

    bool finished_ = false;
    RecordedMidiTake result_;

    std::atomic<std::int64_t> captured_{0};
    std::atomic<bool> rolling_{false};
};

}  // namespace magda::engine
