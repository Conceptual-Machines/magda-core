#include "launch/LaunchHandle.hpp"

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

}  // namespace

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
    pending_ = Pending{QueueState::playQueued, monotonicBeat, {}, {}};
}

void LaunchHandle::stop(std::optional<double> monotonicBeat) {
    pending_ = Pending{QueueState::stopQueued, monotonicBeat, {}, {}};
}

void LaunchHandle::playSynced(const LaunchHandle& other, std::optional<double> monotonicBeat) {
    Pending pending{QueueState::playQueued, monotonicBeat, {}, {}};

    // The origin rather than the launch point, which is the whole difference
    // between this and play(): a handle joining one that is already three bars
    // into its run reports the same played range as it does, so the two agree
    // about where in the material they are instead of merely starting together.
    if (other.played_ && other.playedMonotonic_) {
        pending.syncedTimelineOrigin = other.played_->start;
        pending.syncedMonotonicOrigin = other.playedMonotonic_->start;
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

void LaunchHandle::nudge(double beats) {
    // The origin moves, not the end: the played length is what a source reads
    // its position from, so shifting where the run began is what moves the
    // playhead through the material without interrupting the run.
    if (played_)
        played_->start -= beats;

    if (playedMonotonic_)
        playedMonotonic_->start -= beats;
}

std::optional<BeatRange> LaunchHandle::playedRange() const {
    return played_;
}

std::optional<BeatRange> LaunchHandle::playedMonotonicRange() const {
    return playedMonotonic_;
}

std::optional<BeatRange> LaunchHandle::lastPlayedRange() const {
    return lastPlayed_;
}

void LaunchHandle::beginRun(double timelineBeat, double monotonicBeat, double elapsed) {
    if (played_)
        lastPlayed_ = played_;

    playState_ = PlayState::playing;
    played_ = BeatRange{timelineBeat, timelineBeat + elapsed};
    playedMonotonic_ = BeatRange{monotonicBeat, monotonicBeat + elapsed};
}

void LaunchHandle::endRun() {
    if (played_)
        lastPlayed_ = played_;

    playState_ = PlayState::stopped;
    played_.reset();
    playedMonotonic_.reset();
}

void LaunchHandle::extendRun(const SyncRange& piece) {
    if (!played_ || !playedMonotonic_)
        return;

    // Lengths rather than endpoints. The timeline goes backwards every time the
    // loop wraps and the played range may not, so a run that assigned the
    // block's end beat would shrink at every wrap.
    played_->end += piece.timeline.length();
    playedMonotonic_->end += piece.monotonic.length();
}

void LaunchHandle::applyEvent(bool fromPending, double timelineBeat, double monotonicBeat) {
    if (!fromPending) {
        // A loop re-trigger, which is a relaunch at the wrap: same handle, new
        // run, so the played range restarts and whatever reads it seeks.
        beginRun(timelineBeat, monotonicBeat);
        return;
    }

    const auto pending = *pending_;
    pending_.reset();

    if (pending.state == QueueState::stopQueued) {
        endRun();
        return;
    }

    if (!pending.syncedMonotonicOrigin || !pending.syncedTimelineOrigin) {
        beginRun(timelineBeat, monotonicBeat);
        return;
    }

    // Joining means adopting the position, not just the origin. How far the
    // run it joins has got is derived here rather than copied when the request
    // was made, because the two are different numbers whenever the launch was
    // queued for a later beat, and the one that matters is the one true at the
    // instant it fires.
    beginRun(*pending.syncedTimelineOrigin, *pending.syncedMonotonicOrigin,
             monotonicBeat - *pending.syncedMonotonicOrigin);
}

SplitStatus LaunchHandle::advance(const SyncRange& range) {
    SplitStatus status;
    status.range1 = range.timeline;
    status.range2 = BeatRange{range.timeline.end, range.timeline.end};
    status.playing1 = playState_ == PlayState::playing;
    status.playing2 = status.playing1;

    // What happens inside this block, in monotonic beats. At most one: a
    // handle holds one pending request, so a launch cannot be queued behind a
    // stop, and the only way two events can compete is a request landing in
    // the same block as a loop wrap. The earlier one wins and the other is
    // reconsidered next block, which bounds the error at one block rather than
    // inventing a second split the caller has nowhere to put.
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

    if (playState_ == PlayState::playing && loopBeats_ && playedMonotonic_) {
        const auto origin = playedMonotonic_->start;
        const auto elapsed = range.monotonic.start - origin;
        auto wrap = origin + (std::floor(elapsed / *loopBeats_) + 1.0) * *loopBeats_;

        while (wrap <= range.monotonic.start)
            wrap += *loopBeats_;

        if (wrap < range.monotonic.end && (!event || wrap < *event)) {
            event = wrap;
            fromPending = false;
        }
    }

    if (!event) {
        if (playState_ == PlayState::playing) {
            extendRun(range);
            status.playStartTime1 = played_->start;
        }

        return status;
    }

    // At or before the block's first sample: the whole block is in the new
    // state and there is nothing to split. Reporting a split with an empty
    // first half would be the same answer in a shape every caller has to
    // defend against.
    if (*event <= range.monotonic.start) {
        applyEvent(fromPending, range.timeline.start, range.monotonic.start);

        status.playing1 = playState_ == PlayState::playing;
        status.playing2 = status.playing1;

        if (playState_ == PlayState::playing) {
            extendRun(range);
            status.playStartTime1 = played_->start;
        }

        return status;
    }

    const auto splitBeat = project(range.monotonic, range.timeline, *event);

    const SyncRange first{BeatRange{range.timeline.start, splitBeat},
                          BeatRange{range.monotonic.start, *event}};
    const SyncRange second{BeatRange{splitBeat, range.timeline.end},
                           BeatRange{*event, range.monotonic.end}};

    if (playState_ == PlayState::playing) {
        extendRun(first);
        status.playStartTime1 = played_->start;
    }

    applyEvent(fromPending, second.timeline.start, second.monotonic.start);

    status.playing2 = playState_ == PlayState::playing;

    if (playState_ == PlayState::playing) {
        extendRun(second);
        status.playStartTime2 = played_->start;
    }

    status.isSplit = true;
    status.range1 = first.timeline;
    status.range2 = second.timeline;

    return status;
}

}  // namespace magda::engine
