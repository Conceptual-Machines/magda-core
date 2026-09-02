#include "transport/TransportClock.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace magda::engine {
namespace {

constexpr double kBeatEpsilon = 1.0e-9;

/// A boundary this close to the cursor is the cursor. Rounding up to the first
/// sample at or past a musical position would otherwise turn the position the
/// clock just anchored to into a boundary one sample ahead of itself.
constexpr double kSampleEpsilon = 1.0e-2;

}  // namespace

void TransportClock::anchorTo(const TempoMap& tempo, double beat) {
    anchorSeconds_ = tempo.beatToTime(beat);
    samplesSinceAnchor_ = 0;
    tempoFingerprint_ = tempo.fingerprint();
    positionBeat_ = beat;
    positionBeats_.store(beat, std::memory_order_relaxed);
}

double TransportClock::secondsAfter(std::int64_t samples) const {
    return anchorSeconds_ + static_cast<double>(samples) / sampleRate_;
}

double TransportClock::beatAfter(const TempoMap& tempo, std::int64_t samples) const {
    return tempo.timeToBeat(secondsAfter(samples));
}

std::int64_t TransportClock::samplesUntil(const TempoMap& tempo, double beat) const {
    const auto now = secondsAfter(samplesSinceAnchor_);
    const auto target = tempo.beatToTime(beat);
    return static_cast<std::int64_t>(std::ceil((target - now) * sampleRate_ - kSampleEpsilon));
}

void TransportClock::applyRequest(const TransportSnapshot& snapshot) {
    const auto& request = snapshot.request;
    if (request.generation == generation_)
        return;

    generation_ = request.generation;

    const auto wasPlaying = playing_;
    const auto wasAt = positionBeat_;

    playing_ = request.playing;
    playingPublic_.store(playing_, std::memory_order_relaxed);

    countingIn_ = request.playing && request.countInBeats > 0.0;

    // A request that moves the cursor at all: a locate, or a roll-in, which
    // moves it back to where the count begins. Everything else leaves the
    // anchor alone rather than re-deriving the position it already has.
    if (request.locate || countingIn_) {
        // A locate is honoured as it was asked for, loop or no loop. The loop
        // is somewhere the timeline returns to when it gets there, not a pen: a
        // playhead put down at bar 40 with a two-bar loop enabled plays bar 40,
        // which is what the user pointed at.
        const auto target = request.locate ? request.positionBeat : wasAt;
        const auto position = countingIn_ ? target - request.countInBeats : target;

        countInUntilBeat_ = target;
        anchorTo(snapshot.tempo, position);

        // Arriving somewhere else is a jump.
        if (std::abs(position - wasAt) > kBeatEpsilon)
            continuous_ = false;
    }

    // As is starting. Stopping where the cursor already was is neither, and
    // neither is a request that only repeated what the transport was already
    // doing.
    if (playing_ && !wasPlaying)
        continuous_ = false;
}

void TransportClock::followTempo(const TempoMap& tempo) {
    if (tempo.fingerprint() == tempoFingerprint_)
        return;

    // The cursor keeps its beat and gives up its seconds: an edit to the tempo
    // track moves when a beat happens, not which beat is playing. Continuity
    // survives it, so this is not a jump, and a source reading the timeline has
    // nothing to re-derive that the block's own beats do not already tell it.
    anchorTo(tempo, positionBeat_);
}

std::span<const TransportClock::Segment> TransportClock::advance(const TransportSnapshot& snapshot,
                                                                 double sampleRate,
                                                                 int numSamples) {
    segmentCount_ = 0;

    if (sampleRate > 0.0 && sampleRate != sampleRate_) {
        anchorSeconds_ += static_cast<double>(samplesSinceAnchor_) / sampleRate_;
        samplesSinceAnchor_ = 0;
        sampleRate_ = sampleRate;
    }

    const auto& tempo = snapshot.tempo;

    applyRequest(snapshot);
    followTempo(tempo);

    if (numSamples <= 0)
        return {};

    if (!playing_) {
        const auto beat = beatAfter(tempo, samplesSinceAnchor_);

        const auto seconds = secondsAfter(samplesSinceAnchor_);

        auto& segment = segments_[0];
        segment.block.numSamples = numSamples;
        segment.block.playing = false;
        segment.block.beats.start = beat;
        segment.block.beats.end = beat;
        segment.block.seconds.start = seconds;
        segment.block.seconds.end = seconds;
        segment.block.monotonicBeats.start = monotonicBeat_;
        segment.block.monotonicBeats.end = monotonicBeat_;
        segment.block.monotonicSeconds.start = monotonicSeconds_;
        segment.block.monotonicSeconds.end = monotonicSeconds_;
        segment.block.continuous = continuous_;
        segment.block.tempo = &tempo;
        segment.startSample = 0;
        segment.countingIn = false;

        segmentCount_ = 1;
        continuous_ = true;
        positionBeat_ = beat;
        positionBeats_.store(beat, std::memory_order_relaxed);
        return {segments_.data(), 1};
    }

    auto remaining = numSamples;
    auto offset = 0;

    while (remaining > 0) {
        // The count-in ends at the play position. Not a jump, but it changes
        // what the metronome is allowed to do, so the callback is cut there
        // rather than left to round to whichever side of the beat is nearer.
        if (countingIn_ && samplesUntil(tempo, countInUntilBeat_) <= 0)
            countingIn_ = false;

        // Rolling in is not looping. A count-in that started before the loop
        // end would otherwise wrap on its way to the play position and count
        // for ever without arriving.
        const auto looping = snapshot.loop.valid() && !countingIn_;
        auto untilLoopEnd = looping ? samplesUntil(tempo, snapshot.loop.endBeat)
                                    : std::numeric_limits<std::int64_t>::max();

        // The loop catches a cursor that reached its end, and only that one:
        // one already beyond it was put there, and dragging it back would be
        // ignoring where it was put.
        const auto insideLoop = looping && beatAfter(tempo, samplesSinceAnchor_) <=
                                               snapshot.loop.endBeat + kBeatEpsilon;

        // Back to the top before anything is rendered, so no samples are ever
        // taken from past the end.
        if (insideLoop && untilLoopEnd <= 0) {
            anchorTo(tempo, snapshot.loop.startBeat);
            continuous_ = false;
            untilLoopEnd = samplesUntil(tempo, snapshot.loop.endBeat);
        }

        auto samples = static_cast<std::int64_t>(remaining);
        if (countingIn_)
            samples = std::min(samples, samplesUntil(tempo, countInUntilBeat_));

        // Clamped from inside the loop only. From outside it the count is
        // negative and would cut a callback that is not looping at all; from
        // inside, it can still be zero, for a loop so short that its end is not
        // a sample away from its start even after wrapping. That zero is what
        // routes such a loop into the fallback below rather than letting it
        // render straight through uncounted.
        if (insideLoop)
            samples = std::min(samples, untilLoopEnd);

        // Both boundaries were just moved past if they were behind, so a whole
        // segment is left. What is not left is room to keep cutting: past the
        // last slot, and for a loop too short to have a sample in it, the rest
        // of the callback plays straight through and the overflow is counted.
        auto overflowed = false;
        if (samples < 1 || segmentCount_ + 1 == kMaxSegmentsPerBlock) {
            overflowed = samples < remaining;
            if (overflowed)
                loopWrapOverflows_.fetch_add(1, std::memory_order_relaxed);
            samples = remaining;
        }

        auto& segment = segments_[static_cast<std::size_t>(segmentCount_++)];
        segment.block.numSamples = static_cast<int>(samples);
        segment.block.playing = true;
        segment.block.beats.start = beatAfter(tempo, samplesSinceAnchor_);
        segment.block.beats.end = beatAfter(tempo, samplesSinceAnchor_ + samples);
        segment.block.seconds.start = secondsAfter(samplesSinceAnchor_);
        segment.block.seconds.end = secondsAfter(samplesSinceAnchor_ + samples);

        // Accumulated from the segment rather than read off the cursor: these
        // are the quantities a wrap must not take back, and the cursor is about
        // to be moved by one.
        segment.block.monotonicBeats.start = monotonicBeat_;
        monotonicBeat_ += segment.block.beats.end - segment.block.beats.start;
        segment.block.monotonicBeats.end = monotonicBeat_;

        // From the samples, not the beats: a rate change re-anchors the cursor
        // and this carries straight on (#2324).
        segment.block.monotonicSeconds.start = monotonicSeconds_;
        monotonicSeconds_ += static_cast<double>(samples) / sampleRate_;
        segment.block.monotonicSeconds.end = monotonicSeconds_;

        segment.block.continuous = continuous_;
        segment.block.tempo = &tempo;
        segment.startSample = offset;
        segment.countingIn = countingIn_;

        samplesSinceAnchor_ += samples;
        offset += static_cast<int>(samples);
        remaining -= static_cast<int>(samples);
        continuous_ = true;

        // Rendering straight through left the cursor past the loop end, which
        // is where a cursor that was put there deliberately also sits, and the
        // guard above cannot tell them apart. Put it back at the top: an
        // overflow costs this callback its wrapping, and without this it would
        // cost every callback after it too, because the loop would never catch
        // the cursor again.
        if (overflowed && snapshot.loop.valid() && !countingIn_) {
            anchorTo(tempo, snapshot.loop.startBeat);
            continuous_ = false;
        }
    }

    positionBeat_ = beatAfter(tempo, samplesSinceAnchor_);
    positionBeats_.store(positionBeat_, std::memory_order_relaxed);

    return {segments_.data(), static_cast<std::size_t>(segmentCount_)};
}

}  // namespace magda::engine
