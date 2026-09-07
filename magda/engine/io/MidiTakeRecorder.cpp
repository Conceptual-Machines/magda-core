#include "io/MidiTakeRecorder.hpp"

#include <algorithm>
#include <utility>

#include "exec/EngineDevice.hpp"

namespace magda::engine {

namespace {

constexpr int kMidiChannels = 16;
constexpr int kNotesPerChannel = 128;

/// What one channel message is, as far as the model has a field for it.
enum class Kind : std::uint8_t { noteOn, noteOff, controller, pitchBend, unsupported };

Kind kindOf(const RecordedMidiEvent& event) {
    switch (event.status & 0xf0) {
        case 0x80:
            return Kind::noteOff;

        // Running a note on at zero velocity as a note off is the wire rule,
        // not a courtesy: most controllers never send 0x80 at all.
        case 0x90:
            return event.data2 == 0 ? Kind::noteOff : Kind::noteOn;

        case 0xb0:
            return Kind::controller;

        case 0xe0:
            return Kind::pitchBend;

        default:
            return Kind::unsupported;
    }
}

int heldIndex(const RecordedMidiEvent& event) {
    return ((event.status & 0x0f) * kNotesPerChannel) + (event.data1 & 0x7f);
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

}  // namespace

bool MidiTakeSink::writeMidi(std::span<const RecordedMidiEvent> events) {
    events_.insert(events_.end(), events.begin(), events.end());
    return true;
}

MidiTakeRecorder::MidiTakeRecorder(const LiveInputFeed& feed, MidiTakeRecorderSettings settings)
    : settings_(std::move(settings)),
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

    // Where the wrap is: the timeline the transport has rolled, which is the
    // take's own domain and owes the input's latency nothing. No clamp against
    // what has been queued either, because nothing is committed until finish()
    // -- an event queued early under a negative adjustment crosses this
    // boundary rather than pushing it.
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

    // The one head correction, applied on the way in: everything downstream
    // reads take positions and never has to know what the latency was.
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

/**
 * @brief The events, split into the passes @p edges names and put into beats.
 *
 * One walk in arrival order: a pass is closed when an event lands past its end,
 * which is what makes a note held across a wrap belong to the pass it started
 * in rather than to both.
 */
std::vector<MidiTake> MidiTakeRecorder::passesFrom(const TempoMap& tempo,
                                                   std::span<const std::int64_t> edges) {
    const auto numPasses = edges.size() - 1;
    std::vector<MidiTake> takes(numPasses);

    std::vector<double> passStartBeat(numPasses);
    for (std::size_t pass = 0; pass < numPasses; ++pass)
        passStartBeat[pass] = beatAt(tempo, timeAt(edges[pass]));

    std::vector<HeldNote> held(kMidiChannels * kNotesPerChannel);

    // A note the pass owns, positioned against the pass it started in.
    const auto closeNote = [&](std::size_t pass, int index, std::int64_t endSample) {
        auto& note = held[static_cast<std::size_t>(index)];
        if (note.start < 0)
            return;

        const auto from = beatAt(tempo, timeAt(note.start)) - passStartBeat[pass];
        const auto to = beatAt(tempo, timeAt(endSample)) - passStartBeat[pass];

        // A note whose whole length fell outside the pass never sounded in it.
        if (to > from)
            takes[pass].notes.push_back(MidiNote{.noteNumber = index % kNotesPerChannel,
                                                 .velocity = note.velocity,
                                                 .startBeat = from,
                                                 .lengthBeats = to - from});

        note = {};
    };

    // Every note still down when a pass ends stops there: a note held across a
    // wrap belongs to the pass it started in, once, and the pass is the clip.
    const auto closePass = [&](std::size_t pass) {
        for (auto index = 0; index < kMidiChannels * kNotesPerChannel; ++index)
            closeNote(pass, index, edges[pass + 1]);
    };

    std::size_t pass = 0;

    for (const auto& event : sink_.events()) {
        if (event.sample < edges.front())
            continue;

        if (event.sample >= edges.back())
            break;

        while (pass + 1 < numPasses && event.sample >= edges[pass + 1]) {
            closePass(pass);
            ++pass;
        }

        const auto beatOf = [&] {
            return beatAt(tempo, timeAt(event.sample)) - passStartBeat[pass];
        };

        switch (kindOf(event)) {
            case Kind::noteOn:
                held[static_cast<std::size_t>(heldIndex(event))] = {event.sample, event.data2};
                break;

            case Kind::noteOff:
                closeNote(pass, heldIndex(event), event.sample);
                break;

            case Kind::controller:
                takes[pass].cc.push_back(MidiCCData{.controller = event.data1,
                                                    .value = event.data2,
                                                    .beatPosition = beatOf(),
                                                    .curveType = MidiCurveType::Step});
                break;

            case Kind::pitchBend:
                takes[pass].pitchBend.push_back(
                    MidiPitchBendData{.value = event.data1 | (event.data2 << 7),
                                      .beatPosition = beatOf(),
                                      .curveType = MidiCurveType::Step});
                break;

            // Program change, aftertouch, channel pressure, anything from the
            // system: the model has nowhere to put them, and this is where that
            // is said rather than inside a converter.
            case Kind::unsupported:
                ++result_.messagesDropped;
                break;
        }
    }

    // Whatever was still down when the take ended: a note you were still
    // holding lasted until then.
    for (; pass < numPasses; ++pass)
        closePass(pass);

    // Emitted when they close, so a long note would otherwise sit behind the
    // short ones that started after it.
    for (auto& take : takes)
        std::ranges::sort(take.notes, {}, &MidiNote::startBeat);

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

    // The take's passes as edges in its own positions: it opens at zero, every
    // wrap closes one, and the last block closes the last.
    std::vector<std::int64_t> edges;
    edges.reserve(numBoundaries_ + 2);
    edges.push_back(0);

    for (std::size_t at = 0; at < numBoundaries_; ++at)
        edges.push_back(std::max(boundaries_[at], edges.back()));

    edges.push_back(std::max(end_, edges.back()));

    // A pass of no length is not a pass: a wrap landing on the take's last
    // sample, or a stop between a wrap and the samples it was waiting for.
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());

    if (edges.size() < 2) {
        placeClip(tempo, result_);
        return result_;
    }

    // A first pass that did not begin on the loop start is a lead-in: with no
    // offset of its own it cannot share the clip start the others do, and a
    // wrap having happened at all is what makes it a lead-in rather than the
    // whole recording.
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

    // One pass is an ordinary clip, and the model keeps `takes` empty for one
    // of those: they are loop-record alternatives or nothing.
    if (numPasses > 1) {
        result_.clip.takes = std::move(takes);
        result_.clip.currentTakeIndex = static_cast<int>(active);
    }

    placeClip(tempo, result_);
    return result_;
}

void MidiTakeRecorder::placeClip(const TempoMap& tempo, RecordedMidiTake& take) const {
    // Loop-aligned once a pass boundary has actually been reached, rather than
    // once a wrap has been seen: a take stopped between the two holds only the
    // stretch it began on, and putting that at the loop start would play it
    // somewhere it was never recorded.
    if (numBoundaries_ > 0 && end_ > boundaries_[0]) {
        take.startBeat = loopStartBeat_;
        take.lengthBeats = std::max(0.0, loopEndBeat_ - loopStartBeat_);
        return;
    }

    take.startBeat = startBeat_;
    take.lengthBeats = std::max(0.0, beatAt(tempo, timeAt(end_)) - startBeat_);
}

}  // namespace magda::engine
