#include "clip/ClipMidiSource.hpp"

#include <algorithm>
#include <array>

namespace magda::engine {

namespace {

/// Whether a span reaches into a beat range. Half open at both ends, so a clip
/// ending exactly where a block begins contributes nothing to it.
bool reachesInto(const SnapshotSpan& span, double startBeat, double endBeat) {
    return span.startBeat < endBeat && span.endBeat > startBeat;
}

/// How many notes the source may hold at once before its owed note-offs alone
/// could outrun the port budget. Room for the offs is reserved against this.
constexpr int kOwedCapacity = ActiveNoteList::kChannels * ActiveNoteList::kNotes;

/// Bits for every (channel, note), used to compare what is sounding against
/// what a new snapshot says should be. On the stack: 256 bytes.
class NoteMask {
  public:
    void set(int channel, int note) {
        const auto bit = index(channel, note);
        bits_[bit / 32] |= (1u << (bit % 32));
    }
    bool test(int channel, int note) const {
        const auto bit = index(channel, note);
        return (bits_[bit / 32] & (1u << (bit % 32))) != 0u;
    }

  private:
    static std::size_t index(int channel, int note) {
        return static_cast<std::size_t>((channel - 1) * ActiveNoteList::kNotes + note);
    }

    std::array<std::uint32_t, (ActiveNoteList::kChannels * ActiveNoteList::kNotes) / 32> bits_{};
};

}  // namespace

ClipMidiSource::ClipMidiSource(TrackId trackId, ClipSnapshotFeed& clips)
    : trackId_(trackId), clips_(clips) {}

void ClipMidiSource::prepare(const RenderContext&) {
    // Everything the audio thread may need room for, reserved here and never
    // grown there. Neither is sized from the block: what is owed is bounded by
    // what can be sounding, and what a chase gathers by what one clip holds.
    owed_.reserve(kOwedCapacity);
    scratch_.reserve(kOwedCapacity);
}

bool ClipMidiSource::fits(int bytes) const {
    // Room for this, and for an off for everything currently sounding. A block
    // that spends its last bytes on note-ons is a block that cannot end the
    // notes it started.
    const auto reserved = activeCount_ * kMidiShortMessageBytes;
    return bytesUsed_ + bytes + reserved <= kMaxMidiBytesPerPort;
}

void ClipMidiSource::emit(juce::MidiBuffer& out, int sample, const MidiClipEvent& event) {
    out.addEvent(juce::MidiMessage(event.status, event.data1, event.data2), sample);
    bytesUsed_ += kMidiShortMessageBytes;
}

void ClipMidiSource::endNote(juce::MidiBuffer& out, int sample, int channel, int note) {
    if (!active_.active(channel, note))
        return;

    active_.clear(channel, note);
    activeCount_ = std::max(0, activeCount_ - 1);

    if (bytesUsed_ + kMidiShortMessageBytes <= kMaxMidiBytesPerPort) {
        out.addEvent(juce::MidiMessage::noteOff(channel, note), sample);
        bytesUsed_ += kMidiShortMessageBytes;
        return;
    }

    // Owed rather than dropped. The next block sends it at sample zero, before
    // anything else, which is the one thing that keeps the invariant when the
    // port is full: an off a block late is ten milliseconds of extra tail, an
    // off that never comes is a note that rings until the session ends.
    if (owed_.size() < owed_.capacity())
        owed_.push_back(OwedNote{channel, note});
}

void ClipMidiSource::endAll(juce::MidiBuffer& out, int sample) {
    // Gathered before anything is emitted: endNote mutates the list.
    scratch_.clear();
    active_.forEach([this](int channel, int note) {
        scratch_.push_back(static_cast<std::int32_t>(channel * 1000 + note));
    });

    for (const auto packed : scratch_)
        endNote(out, sample, packed / 1000, packed % 1000);
}

void ClipMidiSource::endClip(juce::MidiBuffer& out, int sample, ClipId clipId) {
    scratch_.clear();
    active_.forEach([this, clipId](int channel, int note) {
        if (active_.owner(channel, note) == clipId)
            scratch_.push_back(static_cast<std::int32_t>(channel * 1000 + note));
    });

    for (const auto packed : scratch_)
        endNote(out, sample, packed / 1000, packed % 1000);
}

bool ClipMidiSource::inHole(const MidiClipPlayback& clip, double beat) {
    return std::any_of(
        clip.silenced.begin(), clip.silenced.end(),
        [beat](const SnapshotSpan& hole) { return beat >= hole.startBeat && beat < hole.endBeat; });
}

void ClipMidiSource::startNote(juce::MidiBuffer& out, const BlockInfo& block,
                               const MidiClipPlayback& clip, std::int32_t index,
                               double timelineBeat) {
    const auto& event = clip.events.events[static_cast<std::size_t>(index)];

    if (inHole(clip, timelineBeat))
        return;  // a hole is a reason for a note not to start

    if (!fits(kMidiShortMessageBytes)) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;  // never entered in the active list, so it never comes to owe an off
    }

    const auto channel = event.channel();
    const auto note = static_cast<int>(event.data1);

    // Already struck, at this instant, by this clip. Two paths reach the same
    // note-on when a discontinuous block lands exactly on a loop pass boundary:
    // the chase strikes what the instant is inside, and the pass start strikes
    // what hangs over it. They are the same note and it is struck once.
    //
    // The exact equality is load-bearing and the two sides reach it by different
    // arithmetic, so do not "simplify" either: the outer chase is handed the
    // block's cropped start and the pass chase computes contentZero plus the
    // window start, and they cancel to the same bits at a wrap. If they ever
    // differ by an ulp the cost is a duplicate note-on at one sample, which a
    // receiver reads as one note, so this is a tidiness guard rather than the
    // invariant's.
    if (active_.active(channel, note) && active_.owner(channel, note) == clip.clipId &&
        active_.startBeat(channel, note) == timelineBeat)
        return;

    emit(out, block.sampleForBeat(timelineBeat), event);

    if (!active_.active(channel, note))
        ++activeCount_;

    active_.start(channel, note, clip.clipId, timelineBeat);
}

void ClipMidiSource::chaseClip(juce::MidiBuffer& out, const BlockInfo& block,
                               const MidiClipPlayback& clip, const MidiFoldPass& pass,
                               double timelineBeat) {
    const auto contentBeat = timelineBeat - pass.timelineOfContentZero;
    const auto sample = block.sampleForBeat(timelineBeat);

    // Controllers first, so a note struck below lands on a configured synth.
    scratch_.clear();
    clip.events.controllerStateAt(contentBeat, scratch_);

    // By index rather than by iterator, and never through a copy: startNote and
    // endNote do not touch the scratch, and copying a reserved vector on the
    // audio thread would allocate, which is the one thing this class may not do.
    for (std::size_t i = 0; i < scratch_.size(); ++i) {
        if (!fits(kMidiShortMessageBytes)) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        emit(out, sample, clip.events.events[static_cast<std::size_t>(scratch_[i])]);
    }

    // Then the notes this instant is inside, which is a question about where
    // they SOUND rather than where they were written: the groove moves both
    // edges independently, so the raw list is only a candidate set. Widened by
    // what the groove can move and then filtered on the grooved edges, so a
    // locate between a note's written onset and its swung one does not start it
    // early, and the inverse miss cannot happen either.
    const auto widen = clip.groove.maxDisplacementBeats();

    scratch_.clear();
    clip.events.notesSoundingAt(contentBeat, widen, scratch_);

    for (std::size_t i = 0; i < scratch_.size(); ++i) {
        const auto index = scratch_[i];
        const auto& event = clip.events.events[static_cast<std::size_t>(index)];

        const auto onBeat = clip.groove.groovyBeat(pass.timelineOfContentZero + event.beat);
        const auto offBeat =
            clip.groove.groovyBeat(pass.timelineOfContentZero +
                                   clip.events.events[static_cast<std::size_t>(event.endsAt)].beat);

        if (onBeat > timelineBeat || offBeat <= timelineBeat)
            continue;

        startNote(out, block, clip, index, timelineBeat);
    }
}

void ClipMidiSource::renderClip(juce::MidiBuffer& out, const BlockInfo& block,
                                const MidiClipPlayback& clip, double from, double to, bool jumped) {
    MidiFoldPass passes[kMaxFoldPassesPerBlock];
    const auto count = foldBlock(clip.fold, from, to, clip.span.endBeat, passes);

    if (count == kMaxFoldPassesPerBlock)
        overflowed_.fetch_add(1, std::memory_order_relaxed);

    const auto& list = clip.events;
    const auto widen = clip.groove.maxDisplacementBeats();

    for (auto index = 0; index < count; ++index) {
        const auto& pass = passes[static_cast<std::size_t>(index)];

        // Notes hanging over the start of the loop region are struck again at
        // it, which is the fork's unrolling read the other way round: its copy
        // of the sequence clips such a note to the pass and keeps what is left.
        if (pass.startsPass)
            chaseClip(out, block, clip, pass, pass.timelineOfContentZero + pass.windowStart);

        // Widened by what the groove can move, and clamped to the pass: an
        // event grooved out of this block is picked up by the next one, whose
        // window is widened the same way at the other end.
        const auto searchFrom = std::max(pass.windowStart, pass.contentStart - widen);
        const auto searchTo = std::min(pass.windowEnd, pass.contentEnd + widen);

        const auto first = list.lowerBound(searchFrom);
        const auto last = list.lowerBound(searchTo);

        // Two walks rather than a scratch sort. juce::MidiBuffer inserts by
        // sample and keeps insertion order for ties, so the output lands in the
        // compile's own order at every sample without one.
        //
        // The split is controllers from note edges, and NOT note-offs from
        // note-ons. Separating those would put a note's off before its on
        // whenever both fall in one callback, and the off would then find
        // nothing active, be skipped, and leave the note sounding until a
        // boundary: MidiBuffer can sort the output, it cannot repair @ref
        // active_. So note edges are walked in list order, which is beat order
        // with the compile's off-before-on tie-break, and a note's own on
        // therefore always precedes its own off however the groove moves either.
        const auto passEndBeat = pass.timelineOfContentZero + pass.windowEnd;

        for (auto phase = 0; phase < 2; ++phase) {
            for (auto i = first; i < last; ++i) {
                const auto& event = list.events[i];

                const auto isOff = event.isNoteOff();
                const auto isOn = event.isNoteOn();
                if ((isOn || isOff) != (phase == 1))
                    continue;

                auto timelineBeat = pass.timelineOfContentZero + event.beat;
                if (event.isNoteEdge())
                    timelineBeat = clip.groove.groovyBeat(timelineBeat);

                if (timelineBeat < from || timelineBeat >= to)
                    continue;

                if (isOn) {
                    // Grooved out of its own pass. The pass end below has
                    // already ended everything the pass started, and MidiBuffer
                    // orders by sample, so emitting this would put the off
                    // before the on and leave a note sounding that nothing here
                    // knows about. The fork drops the same event by clipping its
                    // re-timed sequence to the pass (clipSequenceToRange); the
                    // reason is the invariant rather than the mechanism.
                    if (timelineBeat >= passEndBeat)
                        continue;

                    startNote(out, block, clip, static_cast<std::int32_t>(i), timelineBeat);
                    continue;
                }

                const auto channel = event.channel();
                const auto note = static_cast<int>(event.data1);

                if (isOff) {
                    // Only for a note this source started, and never suppressed
                    // by a hole. Both edges are grooved independently, so an off
                    // can land before its own on: it is held back to it rather
                    // than sent early.
                    if (!active_.active(channel, note) ||
                        active_.owner(channel, note) != clip.clipId)
                        continue;

                    const auto onBeat = active_.startBeat(channel, note);
                    endNote(out, block.sampleForBeat(std::max(timelineBeat, onBeat)), channel,
                            note);
                    continue;
                }

                if (inHole(clip, timelineBeat))
                    continue;

                if (!fits(kMidiShortMessageBytes)) {
                    dropped_.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }

                emit(out, block.sampleForBeat(timelineBeat), event);
            }
        }

        // The pass ran out inside this block, so nothing it started may outlive
        // it. This is what the fold owes the invariant: every note that sounded
        // in a pass is ended inside the same pass.
        if (pass.endsPass)
            endClip(out, block.sampleForBeat(pass.timelineOfContentZero + pass.windowEnd),
                    clip.clipId);
    }

    juce::ignoreUnused(jumped);
}

void ClipMidiSource::render(const BlockInfo& block, juce::MidiBuffer& out) {
    bytesUsed_ = 0;

    // What a previous block could not fit goes out first, before anything can
    // start a note of the same pitch behind it.
    for (const auto& note : owed_) {
        if (bytesUsed_ + kMidiShortMessageBytes > kMaxMidiBytesPerPort)
            break;
        out.addEvent(juce::MidiMessage::noteOff(note.channel, note.note), 0);
        bytesUsed_ += kMidiShortMessageBytes;
    }
    owed_.clear();

    const ClipSnapshotFeed::Reader snapshot(clips_);
    const auto* live = snapshot.get();

    if (live == nullptr) {
        endAll(out, 0);
        lastSnapshot_ = nullptr;
        return;
    }

    const auto* track = live->find(trackId_);

    // A stopped transport does not advance the timeline, so nothing new sounds;
    // what was sounding still has to end. The graph keeps running either way.
    if (!block.playing || track == nullptr || block.numSamples <= 0) {
        endAll(out, 0);
        lastSnapshot_ = live;
        return;
    }

    if (!block.continuous)
        endAll(out, 0);

    if (live != lastSnapshot_) {
        // A swap that moved or deleted what was sounding. Everything the new
        // snapshot agrees should be sounding here is left alone; the rest is
        // ended. The fork's shouldSendNoteOffsForNotesNoLongerPlaying, and it
        // costs one pointer comparison on every block that is not a swap.
        NoteMask expected;

        for (const auto& clip : track->midi) {
            if (!reachesInto(clip.span, block.startBeat, block.endBeat))
                continue;

            MidiFoldPass passes[kMaxFoldPassesPerBlock];
            const auto from = std::max(block.startBeat, clip.span.startBeat);
            if (foldBlock(clip.fold, from, block.endBeat, clip.span.endBeat, passes) == 0)
                continue;

            scratch_.clear();
            clip.events.notesSoundingAt(passes[0].contentStart, clip.groove.maxDisplacementBeats(),
                                        scratch_);

            for (const auto index : scratch_) {
                const auto& event = clip.events.events[static_cast<std::size_t>(index)];
                expected.set(event.channel(), static_cast<int>(event.data1));
            }
        }

        // Gathered before anything is emitted because endNote mutates the list,
        // then walked by index: copying the scratch would allocate here.
        scratch_.clear();
        active_.forEach([this, &expected](int channel, int note) {
            if (!expected.test(channel, note))
                scratch_.push_back(static_cast<std::int32_t>(channel * 1000 + note));
        });

        for (std::size_t i = 0; i < scratch_.size(); ++i)
            endNote(out, 0, scratch_[i] / 1000, scratch_[i] % 1000);

        lastSnapshot_ = live;
    }

    for (const auto& clip : track->midi) {
        // Sorted by where they start, so once one begins at or after the end of
        // this block, so does everything behind it.
        if (clip.span.startBeat >= block.endBeat)
            break;

        if (!reachesInto(clip.span, block.startBeat, block.endBeat)) {
            // Not sounding, and possibly only just: a clip whose span ended in
            // an earlier block has already been ended by that block.
            continue;
        }

        const auto from = std::max(block.startBeat, clip.span.startBeat);
        const auto to = std::min(block.endBeat, clip.span.endBeat);
        if (to <= from)
            continue;

        // Locating into the middle of a clip has to leave every controller at
        // the value its curve is at and strike the notes the instant is inside,
        // or seeking into a sustained pad is silence until the next note.
        if (!block.continuous) {
            MidiFoldPass passes[kMaxFoldPassesPerBlock];
            if (foldBlock(clip.fold, from, to, clip.span.endBeat, passes) > 0)
                chaseClip(out, block, clip, passes[0], from);
        }

        renderClip(out, block, clip, from, to, !block.continuous);

        // The span is the length of a MIDI clip, so its end is where the clip's
        // notes end. Nothing here reads a loop as a length, which is why turning
        // looping off does not shorten a clip the way MidiClip::disableLooping
        // does.
        if (clip.span.endBeat > block.startBeat && clip.span.endBeat <= block.endBeat)
            endClip(out, block.sampleForBeat(clip.span.endBeat), clip.clipId);
    }
}

}  // namespace magda::engine
