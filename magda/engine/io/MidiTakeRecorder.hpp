#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "core/ClipInfo.hpp"
#include "exec/RenderContext.hpp"
#include "io/LiveInput.hpp"
#include "io/RecordStream.hpp"
#include "io/RecordingFeed.hpp"
#include "io/TakeNotes.hpp"
#include "io/TakePasses.hpp"
#include "launch/SessionLauncher.hpp"
#include "tap/RecordTap.hpp"
#include "transport/TempoMap.hpp"
#include "transport/TimeDomains.hpp"
#include "transport/TransportState.hpp"

/**
 * @file MidiTakeRecorder.hpp
 * @brief A MIDI take, from armed to clip (#2462).
 *
 * The MIDI half of io/TakeRecorder.hpp, over the loop rules both share
 * (io/TakePasses.hpp). Events are stamped in the take's own positions on the
 * way into the queue and resolved to beats in finish(), where the tempo map is.
 */

namespace magda::engine {

/** @brief What a finished MIDI take became, and what it cost. */
struct RecordedMidiTake {
    /// Where the clip goes, and how much of the timeline it covers.
    double startBeat = 0.0;
    double lengthBeats = 0.0;

    /// What the clip plays. The model holds these on the clip itself rather
    /// than reaching into `clip.takes` for them.
    MidiTake active;

    /// The passes and the one that plays. `takes` is empty for a single pass.
    MidiClipModel clip;

    /// Whether the take ever rolled. A take that captured no events is still a
    /// take: an empty MIDI clip is a legitimate result.
    bool recorded = false;

    /// Events the queue had no room for, or that were longer than the three
    /// bytes it carries.
    std::int64_t eventsLost = 0;

    /// Events with no field in the model: program change, aftertouch, channel
    /// pressure, anything from the system.
    std::int64_t messagesDropped = 0;

    /// Pass ends that did not fit, which is two loop passes run together.
    std::int64_t passesLost = 0;

    bool empty() const {
        return !recorded;
    }
};

/** @brief What a MIDI take reads, and how much it can hold. */
struct MidiTakeRecorderSettings {
    /// Which input the take listens to, or kAnyLiveMidiSource for all of them.
    LiveMidiSourceId source = kAnyLiveMidiSource;

    /// The input's declared latency, in samples (#2459). An event's place on
    /// the timeline is `arrival - latency`.
    int latencySamples = 0;

    /// How much the queue holds. Its channel count is always zero.
    RecordStreamSettings stream;

    /// The slot whose run this take follows, instead of the transport (#2464).
    /// Absent for an arrangement take.
    std::optional<SlotRunTarget> slot;
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
 * An arrangement take runs from the first block the transport is rolling and
 * not counting in to the first block it is not. A slot take follows a session
 * slot's run instead, beginning and ending on the samples the run did (#2464).
 */
class MidiTakeRecorder final : public TakeCapture {
  public:
    /// @p tap is not owned and outlives the take (#2463).
    MidiTakeRecorder(const LiveInputFeed& feed, RecordTap& tap,
                     const MidiTakeRecorderSettings& settings);

    ~MidiTakeRecorder() override = default;

    /// Neither copied nor moved: the queue holds a reference to the sink.
    MidiTakeRecorder(const MidiTakeRecorder&) = delete;
    MidiTakeRecorder& operator=(const MidiTakeRecorder&) = delete;
    MidiTakeRecorder(MidiTakeRecorder&&) = delete;
    MidiTakeRecorder& operator=(MidiTakeRecorder&&) = delete;

    /// The queue the record thread drains. Registered by whoever owns the
    /// thread, and unregistered before @ref finish.
    RecordStream& stream() override {
        return stream_;
    }

    void capture(const BlockInfo& block, bool countingIn, const LoopRange& loop) override;

    /// Where the pass in flight is published (#2463).
    const RecordTap& tap() const override {
        return tap_;
    }

    /// Events offered to the queue so far. Read from any thread (#2463).
    std::int64_t capturedEvents() const {
        return captured_.load(std::memory_order_relaxed);
    }

    /// Whether the take is still taking blocks. Read from any thread.
    bool rolling() const {
        return rolling_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Close the take and say what it became. Off the audio thread.
     *
     * @p tempo resolves every event's beat, which an audio take needs no
     * parameter for because it converts one position as it goes. Idempotent,
     * and a take that never rolled comes back empty.
     */
    RecordedMidiTake finish(const TempoMap& tempo);

  private:
    enum class State : std::uint8_t { waiting, rolling, stopped };

    /// Pass ends one take can hold, as TakeFileSink's own lane: what will not
    /// fit is refused rather than dropped.
    static constexpr std::size_t kMaxPasses = 256;

    /// @p from is where inside the block the take begins, which a quantized
    /// launch puts off the block boundary.
    void start(const BlockInfo& block, const LoopRange& loop, int from = 0);

    /// One block of a take that follows a slot's run rather than the transport.
    /// A stopped block holds no events but still reports the run's edges, which
    /// is the only block a paused request is reported by.
    void captureRun(const BlockInfo& block);

    /// A wrap: where the pass ended, in the take's own positions.
    void openPass(const BlockInfo& block, const LoopRange& loop);

    void stop();

    /// This block's events over [@p from, @p to), stamped and queued.
    void write(const BlockInfo& block, int from, int to);

    /// The block's events, narrowed to [@p from, @p to) and re-based on @p from.
    void collect(const BlockInfo& block, int from, int to);

    /// This block's notes and the pass's length, as the block leaves them.
    void publish(const BlockInfo& block);

    /// The beat @p sample falls on, through the block's own map: the same
    /// conversion finish() makes, so the preview and the clip place a note
    /// alike unless the tempo is edited mid-take.
    double liveBeatAt(const BlockInfo& block, std::int64_t sample) const {
        return block.beatAtTime(timeAt(sample));
    }

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
    RecordTap& tap_;

    /// This block's events, sized once so a capture cannot allocate.
    juce::MidiBuffer events_;

    /// What the input rendered, before it is narrowed into @ref events_.
    juce::MidiBuffer incoming_;

    /// The pass's notes as the tap holds them, over the table the finished take
    /// walks as well (io/TakeNotes.hpp).
    PreviewNotes preview_{tap_};

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

    /// Where the pass in flight began, in the take's own positions and in the
    /// beats they resolve to. What the tap's notes are relative to, which is
    /// what MidiTake holds them relative to.
    std::int64_t passOrigin_ = 0;
    double passOriginBeat_ = 0.0;

    /// The block's own beat origin, so a beat is read here as the render read it.
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
