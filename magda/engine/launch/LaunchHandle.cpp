#include "launch/LaunchHandle.hpp"

#include <algorithm>
#include <cmath>

namespace magda::engine {

namespace {

/// Where @p value, a position in @p from, falls in @p to. The two ranges are
/// the same stretch of time in two domains, so this is how a monotonic beat
/// becomes the timeline beat at the same instant.
double project(const BeatRange& from, const BeatRange& to, double value) {
    const auto span = from.length();
    if (span <= 0.0)
        return to.start;

    return to.start + ((value - from.start) / span) * to.length();
}

/// The same, from a beat face onto a seconds one. Within one block that is a
/// straight line, which is the same approximation the two beat faces already
/// make of each other.
double project(const BeatRange& from, const SecondsRange& to, double value) {
    const auto span = from.length();
    if (span <= 0.0)
        return to.start;

    return to.start + (((value - from.start) / span) * to.length());
}

}  // namespace

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
    // The seconds face of the same idea, and derived the same way: how long the
    // run has been going comes off the monotonic seconds, and the answer is
    // then expressed on this block's own seconds axis so a source can subtract
    // it from the seconds it was handed.
    //
    // Not the beat origin converted. A tempo map would say where that beat sits
    // rather than how long the run has lasted, and after a wrap the beat it
    // projects to is in a cycle the run never played (#2324).
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
    // All three faces, or the agreement is only musical. A scene of one MIDI
    // clip and one audio clip joined in beats alone would put the notes in the
    // right bar and the samples somewhere else.
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

    // Backwards only as far as the run has got, on each axis by its own length.
    // Further would put the origin in the future, which is a run of negative
    // length and a loop whose next wrap has already passed; a handle cannot be
    // nudged to before it started.
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

void LaunchHandle::beginRun(const SyncPoint& at, double elapsedBeats, double elapsedSeconds) {
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

void LaunchHandle::applyEvent(bool fromPending, const SyncPoint& at) {
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

    // Joining means adopting the position, not just the origin. How far the
    // run it joins has got is derived here rather than copied when the request
    // was made, because the two are different numbers whenever the launch was
    // queued for a later beat, and the one that matters is the one true at the
    // instant it fires. Both faces of "how far", each from its own domain.
    beginRun(SyncPoint{*pending.syncedTimelineOrigin, *pending.syncedMonotonicOrigin,
                       *pending.syncedMonotonicSecondsOrigin},
             at.monotonicBeat - *pending.syncedMonotonicOrigin,
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
    std::optional<double> event;
    auto fromPending = false;

    if (pending_) {
        const auto& position = pending_->position;

        if (!position || *position <= range.monotonic.start) {
            event = range.monotonic.start;
            fromPending = true;
        } else if (*position < range.monotonic.end) {
            event = *position;
            fromPending = true;
        }
    }

    if (!event && playState_ == PlayState::playing && loopBeats_ && playedMonotonic_) {
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
            event = wrap;
            fromPending = false;

            // A second wrap inside the same block is one this cannot report.
            // Counted rather than swallowed: the run carries on past where it
            // was due to restart, and nothing downstream can see that happen.
            if (wrap + *loopBeats_ < range.monotonic.end)
                ++loopRetriggerOverflows_;
        }
    }

    if (!event) {
        if (playState_ == PlayState::playing) {
            status.beforeEvent.origin = RunOrigin{virtualStart(range), virtualStartSeconds(range)};
            extendRun(range);
        }

        return status;
    }

    // At or before the block's first sample: the whole block is in the new
    // state and there is nothing to split. Reporting a split with an empty
    // first half would be the same answer in a shape every caller has to
    // defend against.
    if (*event <= range.monotonic.start) {
        applyEvent(fromPending, SyncPoint{range.timeline.start, range.monotonic.start,
                                          range.monotonicSeconds.start});

        if (playState_ == PlayState::playing) {
            status.beforeEvent.origin = RunOrigin{virtualStart(range), virtualStartSeconds(range)};
            extendRun(range);
        }

        return status;
    }

    // The instant the block ran into, on every axis. Named in monotonic beats,
    // because that is what a queued position is named in, and carried across to
    // the other three by where it falls in this block: within one block that is
    // the only slope there is, and it is the same projection the timeline face
    // has always used.
    const auto splitBeat = project(range.monotonic, range.timeline, *event);
    const auto splitSeconds = project(range.monotonic, range.seconds, *event);
    const auto splitMonotonicSeconds = project(range.monotonic, range.monotonicSeconds, *event);

    const SyncRange first{BeatRange{range.timeline.start, splitBeat},
                          BeatRange{range.monotonic.start, *event},
                          SecondsRange{range.seconds.start, splitSeconds},
                          SecondsRange{range.monotonicSeconds.start, splitMonotonicSeconds}};
    const SyncRange second{BeatRange{splitBeat, range.timeline.end},
                           BeatRange{*event, range.monotonic.end},
                           SecondsRange{splitSeconds, range.seconds.end},
                           SecondsRange{splitMonotonicSeconds, range.monotonicSeconds.end}};

    status.beforeEvent.range = first.timeline;

    if (playState_ == PlayState::playing) {
        status.beforeEvent.origin = RunOrigin{virtualStart(first), virtualStartSeconds(first)};
        extendRun(first);
    }

    applyEvent(fromPending, SyncPoint{second.timeline.start, second.monotonic.start,
                                      second.monotonicSeconds.start});

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
