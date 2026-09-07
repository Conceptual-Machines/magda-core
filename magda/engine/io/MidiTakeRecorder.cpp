#include "io/MidiTakeRecorder.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <utility>

#include "exec/EngineDevice.hpp"

namespace magda::engine {

namespace {

constexpr std::size_t kMidiChannels = 16;
constexpr std::size_t kNotesPerChannel = 128;
constexpr std::size_t kHeldNotes = kMidiChannels * kNotesPerChannel;

constexpr unsigned kTypeMask = 0xf0U;
constexpr unsigned kChannelMask = 0x0fU;
constexpr unsigned kDataMask = 0x7fU;

/// What one channel message is, as far as the model has a field for it.
enum class Kind : std::uint8_t { noteOn, noteOff, controller, pitchBend, unsupported };

Kind kindOf(const RecordedMidiEvent& event) {
    switch (static_cast<unsigned>(event.status) & kTypeMask) {
        case 0x80U:
            return Kind::noteOff;

        // Note on at zero velocity is a note off: the wire rule, and most
        // controllers never send 0x80 at all.
        case 0x90U:
            return event.data2 == 0 ? Kind::noteOff : Kind::noteOn;

        case 0xb0U:
            return Kind::controller;

        case 0xe0U:
            return Kind::pitchBend;

        default:
            return Kind::unsupported;
    }
}

/// Where a channel's note sits in the held table.
std::size_t heldIndex(const RecordedMidiEvent& event) {
    const auto channel = static_cast<unsigned>(event.status) & kChannelMask;
    const auto note = static_cast<unsigned>(event.data1) & kDataMask;
    return (static_cast<std::size_t>(channel) * kNotesPerChannel) + note;
}

/// The 14 bits a pitch wheel splits across its two data bytes.
int pitchBendValue(const RecordedMidiEvent& event) {
    const auto low = static_cast<unsigned>(event.data1) & kDataMask;
    const auto high = static_cast<unsigned>(event.data2) & kDataMask;
    return static_cast<int>((high << 7U) | low);
}

RecordStreamSettings queueFor(const MidiTakeRecorderSettings& settings) {
    auto queue = settings.stream;
    queue.numChannels = 0;
    return queue;
}

/// A note that has sounded and not yet stopped.
struct HeldNote {
    std::int64_t start = -1;
    int velocity = 0;
};

/**
 * @brief A take's events, split into the passes an edge list names, in beats.
 *
 * Fed in arrival order. A pass is closed when an event lands past its end, so a
 * note held across a wrap belongs to the pass it started in.
 */
class PassWalk {
  public:
    /// @p beatOf gives the take-relative beat of a take position.
    PassWalk(std::span<const std::int64_t> edges, std::function<double(std::int64_t)> beatOf)
        : edges_(edges), beatOf_(std::move(beatOf)), takes_(edges.size() - 1), held_(kHeldNotes) {
        passStart_.reserve(takes_.size());

        for (std::size_t pass = 0; pass < takes_.size(); ++pass)
            passStart_.push_back(beatOf_(edges_[pass]));
    }

    void add(const RecordedMidiEvent& event);

    /// Close every pass still open and hand the takes over.
    std::vector<MidiTake> close();

    /// Events with no field in the model.
    std::int64_t dropped() const {
        return dropped_;
    }

  private:
    double beatIn(std::size_t pass, std::int64_t sample) const {
        return beatOf_(sample) - passStart_[pass];
    }

    void closeNote(std::size_t pass, std::size_t index, std::int64_t endSample);
    void closePass(std::size_t pass);

    std::span<const std::int64_t> edges_;
    std::function<double(std::int64_t)> beatOf_;
    std::vector<double> passStart_;
    std::vector<MidiTake> takes_;
    std::vector<HeldNote> held_;

    std::size_t pass_ = 0;
    std::int64_t dropped_ = 0;
};

/// A note the pass owns, positioned against the pass it started in.
void PassWalk::closeNote(std::size_t pass, std::size_t index, std::int64_t endSample) {
    auto& note = held_[index];
    if (note.start < 0)
        return;

    const auto from = beatIn(pass, note.start);
    const auto to = beatIn(pass, endSample);

    // A note whose length fell wholly outside the pass never sounded in it.
    if (to > from) {
        MidiNote played;
        played.noteNumber = static_cast<int>(index % kNotesPerChannel);
        played.velocity = note.velocity;
        played.startBeat = from;
        played.lengthBeats = to - from;
        takes_[pass].notes.push_back(played);
    }

    note = {};
}

// A note held across a wrap belongs to the pass it started in, once.
void PassWalk::closePass(std::size_t pass) {
    for (std::size_t index = 0; index < kHeldNotes; ++index)
        closeNote(pass, index, edges_[pass + 1]);
}

void PassWalk::add(const RecordedMidiEvent& event) {
    if (event.sample < edges_.front() || event.sample >= edges_.back())
        return;

    while (pass_ + 1 < takes_.size() && event.sample >= edges_[pass_ + 1]) {
        closePass(pass_);
        ++pass_;
    }

    switch (kindOf(event)) {
        case Kind::noteOn:
            held_[heldIndex(event)] = {.start = event.sample, .velocity = event.data2};
            break;

        case Kind::noteOff:
            closeNote(pass_, heldIndex(event), event.sample);
            break;

        case Kind::controller: {
            MidiCCData cc;
            cc.controller = event.data1;
            cc.value = event.data2;
            cc.beatPosition = beatIn(pass_, event.sample);
            cc.curveType = MidiCurveType::Step;
            takes_[pass_].cc.push_back(cc);
            break;
        }

        case Kind::pitchBend: {
            MidiPitchBendData bend;
            bend.value = pitchBendValue(event);
            bend.beatPosition = beatIn(pass_, event.sample);
            bend.curveType = MidiCurveType::Step;
            takes_[pass_].pitchBend.push_back(bend);
            break;
        }

        // Program change, aftertouch, pressure, system: no field to put them
        // in, counted here rather than dropped inside a converter.
        case Kind::unsupported:
            ++dropped_;
            break;
    }
}

std::vector<MidiTake> PassWalk::close() {
    // A note still down when the take ended lasted until it did.
    for (; pass_ < takes_.size(); ++pass_)
        closePass(pass_);

    // Emitted when they close, so a long note lands behind later short ones.
    for (auto& take : takes_)
        std::ranges::sort(take.notes, {}, &MidiNote::startBeat);

    return std::move(takes_);
}

}  // namespace

bool MidiTakeSink::writeMidi(std::span<const RecordedMidiEvent> events) {
    events_.insert(events_.end(), events.begin(), events.end());
    return true;
}

MidiTakeRecorder::MidiTakeRecorder(const LiveInputFeed& feed,
                                   const MidiTakeRecorderSettings& settings)
    : settings_(settings),
      input_(feed, settings_.source, settings_.latencySamples),
      stream_(sink_, queueFor(settings_)) {
    // Sized once, so a block's events are copied into it and never allocate.
    events_.ensureSize(static_cast<std::size_t>(kMaxMidiBytesPerPort));
}

void MidiTakeRecorder::capture(const BlockInfo& block, bool countingIn, const LoopRange& loop) {
    if (state_ == State::stopped)
        return;

    // A count-in is time before the play position and a stop is where a take
    // ends, so neither is part of one.
    if (!block.playing || countingIn) {
        if (state_ == State::rolling)
            stop();

        return;
    }

    if (state_ == State::waiting) {
        start(block, loop);
    } else if (!block.continuous) {
        if (!atLoopStart(block, loop)) {
            stop();
            return;
        }

        openPass(loop);
    }

    write(block);
    arrivals_ += block.numSamples;
}

void MidiTakeRecorder::start(const BlockInfo& block, const LoopRange& loop) {
    state_ = State::rolling;
    rolled_ = true;
    rolling_.store(true, std::memory_order_relaxed);

    startBeat_ = block.beats.start;
    startSeconds_ = block.seconds.start;
    sampleRate_ = block.rate();
    origin_ = block.materialOrigin;
    startedAtLoopStart_ = atLoopStart(block, loop);
}

void MidiTakeRecorder::openPass(const LoopRange& loop) {
    loopStartBeat_ = loop.startBeat;
    loopEndBeat_ = loop.endBeat;

    // The timeline the transport has rolled, which owes the input's latency
    // nothing. Unclamped: nothing is committed until finish(), so an event
    // queued early crosses this boundary rather than pushing it.
    if (numBoundaries_ == kMaxPasses) {
        ++boundariesLost_;
        return;
    }

    boundaries_[numBoundaries_++] = arrivals_;
}

void MidiTakeRecorder::stop() {
    state_ = State::stopped;
    rolling_.store(false, std::memory_order_relaxed);
}

void MidiTakeRecorder::write(const BlockInfo& block) {
    events_.clear();
    input_.render(block, events_);

    end_ = arrivals_ + block.numSamples - settings_.latencySamples;

    if (events_.isEmpty())
        return;

    // The one head correction, applied on the way in.
    stream_.writeMidi(events_, arrivals_ - settings_.latencySamples);
    captured_.fetch_add(events_.getNumEvents(), std::memory_order_relaxed);
}

double MidiTakeRecorder::timeAt(std::int64_t sample) const {
    if (sampleRate_ <= 0.0)
        return startSeconds_;

    return startSeconds_ + (static_cast<double>(sample) / sampleRate_);
}

double MidiTakeRecorder::beatAt(const TempoMap& tempo, double moment) const {
    return tempo.timeToBeat(moment + origin_.seconds) - origin_.beat;
}

std::vector<MidiTake> MidiTakeRecorder::passesFrom(const TempoMap& tempo,
                                                   std::span<const std::int64_t> edges) {
    PassWalk walk(edges, [&](std::int64_t sample) { return beatAt(tempo, timeAt(sample)); });

    for (const auto& event : sink_.events())
        walk.add(event);

    auto takes = walk.close();
    result_.messagesDropped += walk.dropped();
    return takes;
}

RecordedMidiTake MidiTakeRecorder::finish(const TempoMap& tempo) {
    if (finished_)
        return result_;

    finished_ = true;
    stop();
    stream_.finish();

    result_.eventsLost = stream_.eventsLost();
    result_.passesLost = boundariesLost_;
    result_.recorded = rolled_;

    if (!rolled_) {
        placeClip(tempo, result_);
        return result_;
    }

    // Pass edges in the take's own positions: it opens at zero, every wrap
    // closes one, and the last block closes the last.
    std::vector<std::int64_t> edges;
    edges.reserve(numBoundaries_ + 2);
    edges.push_back(0);

    for (std::size_t at = 0; at < numBoundaries_; ++at)
        edges.push_back(std::max(boundaries_[at], edges.back()));

    edges.push_back(std::max(end_, edges.back()));

    // A pass of no length is not a pass.
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());

    if (edges.size() < 2) {
        placeClip(tempo, result_);
        return result_;
    }

    // A first pass that did not begin on the loop start is a lead-in: MidiTake
    // has no offset of its own, so it cannot share the clip start.
    if (edges.size() > 2 && !startedAtLoopStart_)
        edges.erase(edges.begin());

    const auto numPasses = edges.size() - 1;
    auto takes = passesFrom(tempo, edges);

    std::vector<std::int64_t> lengths;
    lengths.reserve(numPasses);
    for (std::size_t at = 0; at < numPasses; ++at)
        lengths.push_back(edges[at + 1] - edges[at]);

    const auto active = activeTake(lengths);
    result_.active = takes[active];

    // The model keeps `takes` empty for a single pass: they are loop-record
    // alternatives or nothing.
    if (numPasses > 1) {
        result_.clip.takes = std::move(takes);
        result_.clip.currentTakeIndex = static_cast<int>(active);
    }

    placeClip(tempo, result_);
    return result_;
}

void MidiTakeRecorder::placeClip(const TempoMap& tempo, RecordedMidiTake& take) const {
    // Loop-aligned once a boundary has been reached, not once a wrap has been
    // seen: a take stopped between the two holds only the stretch it began on.
    if (numBoundaries_ > 0 && end_ > boundaries_[0]) {
        take.startBeat = loopStartBeat_;
        take.lengthBeats = std::max(0.0, loopEndBeat_ - loopStartBeat_);
        return;
    }

    take.startBeat = startBeat_;
    take.lengthBeats = std::max(0.0, beatAt(tempo, timeAt(end_)) - startBeat_);
}

}  // namespace magda::engine
