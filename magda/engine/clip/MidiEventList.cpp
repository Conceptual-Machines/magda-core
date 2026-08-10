#include "clip/MidiEventList.hpp"

#include <algorithm>
#include <cmath>

namespace magda::engine {

namespace {

/// Floor division that behaves for negative numerators, which a clip whose
/// offset runs backwards produces.
double floorDiv(double value, double divisor) {
    return std::floor(value / divisor);
}

}  // namespace

std::size_t MidiEventList::lowerBound(double beat) const {
    const auto found =
        std::lower_bound(events.begin(), events.end(), beat,
                         [](const MidiClipEvent& event, double b) { return event.beat < b; });
    return static_cast<std::size_t>(std::distance(events.begin(), found));
}

void MidiEventList::controllerStateAt(double beat, std::vector<std::int32_t>& out) const {
    for (const auto& stream : controllers) {
        // Strictly before, never at: an event sitting exactly on the instant is
        // about to be walked by the block itself, and chasing it as well would
        // send it twice.
        const auto found = std::lower_bound(
            stream.events.begin(), stream.events.end(), beat, [this](std::int32_t index, double b) {
                return events[static_cast<std::size_t>(index)].beat < b;
            });

        if (found == stream.events.begin())
            continue;  // nothing before the instant, so no value to chase to

        out.push_back(*std::prev(found));
    }
}

void MidiEventList::notesSoundingAt(double beat, double widen,
                                    std::vector<std::int32_t>& out) const {
    if (longestNoteBeats <= 0.0)
        return;

    // No note-on further back than the longest note can still be sounding, so
    // the walk is bounded by the material rather than by the clip's length.
    // @p widen is what a groove can move an edge by: with one in force these are
    // candidates and the caller decides on the grooved edges, so the window has
    // to cover a note the groove has not moved into it yet.
    auto index = lowerBound(beat - longestNoteBeats - widen);
    const auto end = lowerBound(beat + widen);

    for (; index < end; ++index) {
        const auto& event = events[index];
        if (!event.isNoteOn() || event.endsAt < 0)
            continue;

        if (events[static_cast<std::size_t>(event.endsAt)].beat > beat - widen)
            out.push_back(static_cast<std::int32_t>(index));
    }
}

int foldBlock(const MidiFold& fold, double startBeat, double endBeat, double clipEndBeat,
              MidiFoldPass (&out)[kMaxFoldPassesPerBlock]) {
    if (endBeat <= startBeat)
        return 0;

    if (!fold.loopEnabled || fold.loopLengthBeats <= 0.0) {
        // One pass, and its window is the whole clip: what crops it is the
        // span, which the caller has already applied. Not a second visible
        // range, which is what the incumbent needs only because TE requires the
        // sequence to BE the clip.
        auto& pass = out[0];
        pass.timelineOfContentZero = fold.clipStartBeat - fold.trimOffsetBeats - fold.offsetBeats;
        pass.contentStart = startBeat - pass.timelineOfContentZero;
        pass.contentEnd = endBeat - pass.timelineOfContentZero;
        pass.windowStart = fold.clipStartBeat - pass.timelineOfContentZero;
        pass.windowEnd = clipEndBeat - pass.timelineOfContentZero;
        pass.startsPass = startBeat <= fold.clipStartBeat && fold.clipStartBeat < endBeat;
        pass.endsPass = startBeat < clipEndBeat && clipEndBeat <= endBeat;
        return 1;
    }

    const auto length = fold.loopLengthBeats;
    const auto elapsedStart = startBeat - fold.clipStartBeat + fold.offsetBeats;
    const auto elapsedEnd = endBeat - fold.clipStartBeat + fold.offsetBeats;

    const auto firstPass = floorDiv(elapsedStart, length);
    // The end is half open, so a block ending exactly on a wrap belongs to the
    // pass it played rather than opening the one it did not reach.
    const auto lastPass = floorDiv(std::nextafter(elapsedEnd, elapsedStart), length);

    auto count = 0;

    for (auto pass = firstPass; pass <= lastPass && count < kMaxFoldPassesPerBlock; pass += 1.0) {
        const auto passStart = pass * length;
        const auto passEnd = passStart + length;

        const auto from = std::max(elapsedStart, passStart);
        const auto to = std::min(elapsedEnd, passEnd);
        if (to <= from)
            continue;

        auto& entry = out[count++];
        entry.timelineOfContentZero =
            fold.clipStartBeat - fold.offsetBeats + passStart - fold.loopStartBeats;
        entry.contentStart = fold.loopStartBeats + (from - passStart);
        entry.contentEnd = fold.loopStartBeats + (to - passStart);
        entry.windowStart = fold.loopStartBeats;
        entry.windowEnd = fold.loopStartBeats + length;
        entry.startsPass = elapsedStart <= passStart;
        entry.endsPass = elapsedEnd >= passEnd;
    }

    return count;
}

}  // namespace magda::engine
