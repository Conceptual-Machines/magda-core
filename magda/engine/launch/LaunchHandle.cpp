#include "launch/LaunchHandle.hpp"

#include <algorithm>
#include <cmath>

namespace magda::engine {

double LaunchHandle::virtualStart(const SyncRange& piece) const {
    // Where the run would have begun on the timeline as it stands now, which
    // is not where it did begin once the timeline has wrapped or been located
    // since. A source works out how far into the material it is by subtracting
    // this from the block it was handed, so the two have to be in the same
    // cycle of the timeline; the beat the run actually started on stops being
    // in any cycle the moment the loop takes it back.
    //
    // Derived from monotonic elapsed, which is the one quantity that survives
    // the wrap. It may sit before the block or before zero, and that is what it
    // means: the run began that far back.
    return piece.timeline.start - (piece.monotonic.start - playedMonotonic_->start);
}

double LaunchHandle::virtualStartSeconds(const SyncRange& piece) const {
    // The same, off the monotonic seconds. Not the beat origin converted: a map
    // says where a beat sits, not how long a run has lasted (#2324).
    return piece.seconds.start - (piece.monotonicSeconds.start - playedMonotonicSeconds_->start);
}

std::optional<LaunchHandle::QueueState> LaunchHandle::queuedState() const {
    if (!pending_)
        return {};

    return pending_->state;
}

std::optional<double> LaunchHandle::queuedPosition() const {
    if (!pending_)
        return {};

    return pending_->position;
}

void LaunchHandle::play(std::optional<double> monotonicBeat) {
    pending_ = Pending{QueueState::playQueued, monotonicBeat, {}, {}, {}};
}

void LaunchHandle::stop(std::optional<double> monotonicBeat) {
    pending_ = Pending{QueueState::stopQueued, monotonicBeat, {}, {}, {}};
}

void LaunchHandle::playSynced(const LaunchHandle& other, std::optional<double> monotonicBeat) {
    Pending pending{QueueState::playQueued, monotonicBeat, {}, {}, {}};

    // The origin rather than the launch point, which is the whole difference
    // between this and play(): a handle joining one that is already three bars
    // into its run reports the same played range as it does, so the two agree
    // about where in the material they are instead of merely starting together.
    //
    // All three faces, or the agreement is only musical.
    if (other.played_ && other.playedMonotonic_ && other.playedMonotonicSeconds_) {
        pending.syncedTimelineOrigin = other.played_->start;
        pending.syncedMonotonicOrigin = other.playedMonotonic_->start;
        pending.syncedMonotonicSecondsOrigin = other.playedMonotonicSeconds_->start;
    }

    pending_ = pending;
}

void LaunchHandle::setLooping(std::optional<double> beats) {
    if (beats && *beats <= 0.0) {
        loopBeats_.reset();
        return;
    }

    loopBeats_ = beats;
}

void LaunchHandle::nudge(double beats, double seconds) {
    // The origin moves, not the end: the played length is what a source reads
    // its position from, so shifting where the run began is what moves the
    // playhead through the material without interrupting the run.
    if (!played_ || !playedMonotonic_ || !playedMonotonicSeconds_)
        return;

    // Backwards only as far as the run has got, each axis by its own length.
    // Further would put the origin in the future.
    const auto effectiveBeats = std::max(beats, -played_->length());
    const auto effectiveSeconds = std::max(seconds, -playedMonotonicSeconds_->length());

    played_->start -= effectiveBeats;
    playedMonotonic_->start -= effectiveBeats;
    playedMonotonicSeconds_->start -= effectiveSeconds;
}

std::optional<BeatRange> LaunchHandle::playedRange() const {
    return played_;
}

std::optional<BeatRange> LaunchHandle::playedMonotonicRange() const {
    return playedMonotonic_;
}

std::optional<SecondsRange> LaunchHandle::playedMonotonicSecondsRange() const {
    return playedMonotonicSeconds_;
}

std::optional<BeatRange> LaunchHandle::lastPlayedRange() const {
    return lastPlayed_;
}

void LaunchHandle::beginRun(const BlockInstant& at, double elapsedBeats, double elapsedSeconds) {
    if (played_)
        lastPlayed_ = played_;

    playState_ = PlayState::playing;
    played_ = BeatRange{at.timelineBeat, at.timelineBeat + elapsedBeats};
    playedMonotonic_ = BeatRange{at.monotonicBeat, at.monotonicBeat + elapsedBeats};
    playedMonotonicSeconds_ =
        SecondsRange{at.monotonicSeconds, at.monotonicSeconds + elapsedSeconds};
}

void LaunchHandle::endRun() {
    if (played_)
        lastPlayed_ = played_;

    playState_ = PlayState::stopped;
    played_.reset();
    playedMonotonic_.reset();
    playedMonotonicSeconds_.reset();
}

void LaunchHandle::extendRun(const SyncRange& piece) {
    if (!played_ || !playedMonotonic_ || !playedMonotonicSeconds_)
        return;

    // Lengths rather than endpoints. The timeline goes backwards every time the
    // loop wraps and the played range may not, so a run that assigned the
    // block's end beat would shrink at every wrap.
    played_->end += piece.timeline.length();
    playedMonotonic_->end += piece.monotonic.length();
    playedMonotonicSeconds_->end += piece.monotonicSeconds.length();
}

void LaunchHandle::applyEvent(bool fromPending, const BlockInstant& at) {
    if (!fromPending) {
        // A loop re-trigger, which is a relaunch at the wrap: same handle, new
        // run, so the played range restarts and whatever reads it seeks.
        beginRun(at);
        return;
    }

    const auto pending = *pending_;
    pending_.reset();

    if (pending.state == QueueState::stopQueued) {
        endRun();
        return;
    }

    if (!pending.syncedMonotonicOrigin || !pending.syncedTimelineOrigin ||
        !pending.syncedMonotonicSecondsOrigin) {
        beginRun(at);
        return;
    }

    // Joining means adopting the position, not just the origin. Derived here
    // rather than at the request, which is a different number whenever the
    // launch was queued for a later beat.
    // The origin is another handle's, so it is not an instant in this block and
    // is carried as the three faces that handle recorded rather than derived.
    BlockInstant origin = at;
    origin.timelineBeat = *pending.syncedTimelineOrigin;
    origin.monotonicBeat = *pending.syncedMonotonicOrigin;
    origin.monotonicSeconds = *pending.syncedMonotonicSecondsOrigin;

    beginRun(origin, at.monotonicBeat - *pending.syncedMonotonicOrigin,
             at.monotonicSeconds - *pending.syncedMonotonicSecondsOrigin);
}

SplitStatus LaunchHandle::advance(const SyncRange& range) {
    blockStatus_ = advanceOver(range);
    return blockStatus_;
}

SplitStatus LaunchHandle::advanceOver(const SyncRange& range) {
    SplitStatus status;
    status.beforeEvent.range = range.timeline;

    // What happens inside this block, in monotonic beats. At most one, and a
    // request always outranks a loop wrap.
    //
    // The rule is that anything quantized lands on its beat, and anything that
    // is not is the user's own business. A wrap is the handle's bookkeeping and
    // it is not entitled to move an instant somebody asked for: a clip launched
    // off the grid has its wraps off the grid too, and letting one displace a
    // quantized stop would carry that drift into the one action that was
    // explicitly put on the beat.
    //
    // Giving way costs the wrap nothing. A stop ends the run and a launch
    // restarts it, so either way the run it would have restarted is gone before
    // the block is over.
    std::optional<double> eventBeat;
    auto fromPending = false;

    if (pending_) {
        const auto& position = pending_->position;

        if (!position || *position <= range.monotonic.start) {
            eventBeat = range.monotonic.start;
            fromPending = true;
        } else if (*position < range.monotonic.end) {
            eventBeat = *position;
            fromPending = true;
        }
    }

    if (!eventBeat && playState_ == PlayState::playing && loopBeats_ && playedMonotonic_) {
        const auto origin = playedMonotonic_->start;
        const auto elapsed = range.monotonic.start - origin;

        // The first wrap at or after this block begins, which is ceil rather
        // than floor-plus-one: a wrap due exactly on the first sample was
        // passed over by the previous block, whose range ended before it, and
        // rounding up again here would skip it a second time and every
        // block-aligned wrap after it. Never the origin itself, which is where
        // the run started rather than somewhere it repeats.
        const auto turns = std::max(1.0, std::ceil(elapsed / *loopBeats_));
        auto wrap = origin + turns * *loopBeats_;

        while (wrap < range.monotonic.start)
            wrap += *loopBeats_;

        if (wrap < range.monotonic.end) {
            eventBeat = wrap;
            fromPending = false;

            // A second wrap inside the same block is one this cannot report.
            // Counted rather than swallowed: the run carries on past where it
            // was due to restart, and nothing downstream can see that happen.
            if (wrap + *loopBeats_ < range.monotonic.end)
                ++loopRetriggerOverflows_;
        }
    }

    if (!eventBeat) {
        if (playState_ == PlayState::playing) {
            status.beforeEvent.origin = RunOrigin{virtualStart(range), virtualStartSeconds(range)};
            extendRun(range);
        }

        return status;
    }

    // The instant the block ran into, worked out once and in one domain. Every
    // face of it below comes off the sample it landed on, so the two halves and
    // the origin they produce cannot disagree about where the cut was (#2330).
    const auto event = range.eventAtMonotonicBeat(*eventBeat);
    status.event = event;

    // At or before the block's first sample: the whole block is in the new
    // state and there is nothing to split. Reporting a split with an empty
    // first half would be the same answer in a shape every caller has to
    // defend against.
    if (event.sample <= 0) {
        applyEvent(fromPending, range.start());

        if (playState_ == PlayState::playing) {
            status.beforeEvent.origin = RunOrigin{virtualStart(range), virtualStartSeconds(range)};
            extendRun(range);
        }

        return status;
    }

    const SyncRange first{BeatRange{range.timeline.start, event.timelineBeat},
                          BeatRange{range.monotonic.start, event.monotonicBeat},
                          SecondsRange{range.seconds.start, event.timelineSeconds},
                          SecondsRange{range.monotonicSeconds.start, event.monotonicSeconds},
                          event.sample,
                          range.tempo};
    const SyncRange second{BeatRange{event.timelineBeat, range.timeline.end},
                           BeatRange{event.monotonicBeat, range.monotonic.end},
                           SecondsRange{event.timelineSeconds, range.seconds.end},
                           SecondsRange{event.monotonicSeconds, range.monotonicSeconds.end},
                           range.numSamples - event.sample,
                           range.tempo};

    status.beforeEvent.range = first.timeline;

    if (playState_ == PlayState::playing) {
        status.beforeEvent.origin = RunOrigin{virtualStart(first), virtualStartSeconds(first)};
        extendRun(first);
    }

    applyEvent(fromPending, event);

    BlockPiece after;
    after.range = second.timeline;

    if (playState_ == PlayState::playing) {
        after.origin = RunOrigin{virtualStart(second), virtualStartSeconds(second)};
        extendRun(second);
    }

    status.afterEvent = after;
    return status;
}

}  // namespace magda::engine
