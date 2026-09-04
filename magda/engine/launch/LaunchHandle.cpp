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
    return piece.timeline.start - (piece.monotonic.start - run_->originBeat);
}

double LaunchHandle::elapsedSecondsAt(const SyncRange& piece) const {
    // A subtraction of two sample counts, which is exact, rather than an
    // accumulation of per-block seconds. Between rate changes: a count that
    // spans one counts samples that were not all worth the same amount of time,
    // which is what followRate is for.
    return (piece.monotonicSamples.start - run_->origin).seconds(run_->sampleRate);
}

double LaunchHandle::virtualStartSeconds(const SyncRange& piece) const {
    // The same as virtualStart, on the seconds axis. Not the beat origin
    // converted: a map says where a beat sits, not how long a run has lasted
    // (#2324).
    return piece.seconds.start - elapsedSecondsAt(piece);
}

void LaunchHandle::recountRun(Run& run, const SyncRange& piece) {
    // The device changed rate under a run. The origin is a count of samples and
    // the samples are not the same length any more, so holding the number would
    // silently change how far into its material the run is. What is kept
    // instead is the elapsed time it has actually had, and the origin is
    // re-counted behind the block at the new rate.
    if (run.sampleRate > 0.0) {
        const auto elapsed = (piece.monotonicSamples.start - run.origin).seconds(run.sampleRate);
        run.origin =
            piece.monotonicSamples.start - SampleDuration::ofSeconds(elapsed, piece.rate());
        run.through = std::max(run.through, run.origin);
    }

    run.sampleRate = piece.rate();
}

void LaunchHandle::followRate(const SyncRange& piece) {
    if (piece.rate() <= 0.0)
        return;

    // Which depends on this block being the first at the new rate, and that is
    // an invariant rather than a guess: advanceLaunchHandles advances every
    // handle over every block before anything renders (SessionLauncher.hpp), so
    // a handle cannot miss the block a change arrives on.
    if (run_ && piece.rate() != run_->sampleRate)
        recountRun(*run_, piece);

    // The run a synced launch is waiting to join, on the same blocks. It was
    // copied when the launch was requested and is joined whenever its beat
    // arrives, and a rate change in between would otherwise leave the follower
    // adopting an origin counted in samples of another length: at 44.1 to 48
    // kHz with a second between the two, the follower would sit about 90 ms off
    // the leader for the rest of the run.
    //
    // Carried rather than re-read from the leader at the launch instant, which
    // would be the other way to keep it current. A handle has no safe way to
    // name another one across the blocks in between: the store frees a handle
    // the next published table does not name, and a pending request holding a
    // pointer to one would be holding it after it went. Lockstep gets the same
    // answer, because the copy and the leader's own run start identical and
    // every handle is advanced over every block.
    if (pending_ && pending_->synced && piece.rate() != pending_->synced->sampleRate)
        recountRun(*pending_->synced, piece);
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
    pending_ = Pending{QueueState::playQueued, monotonicBeat, {}, false};
}

void LaunchHandle::stop(std::optional<double> monotonicBeat) {
    pending_ = Pending{QueueState::stopQueued, monotonicBeat, {}, false};
}

void LaunchHandle::playSynced(const LaunchHandle& other, std::optional<double> monotonicBeat) {
    // The run rather than the launch point, which is the whole difference
    // between this and play(): a handle joining one that is already three bars
    // into its run reports the same played range as it does, so the two agree
    // about where in the material they are instead of merely starting together.
    //
    // The whole run, because its faces are one instant. Copying some of them
    // and deriving the rest is how two handles end up agreeing about the bar
    // and not the sample.
    pending_ = Pending{QueueState::playQueued, monotonicBeat, other.run_, false};
}

void LaunchHandle::setLooping(std::optional<double> beats) {
    if (beats && *beats <= 0.0) {
        loopBeats_.reset();
        return;
    }

    loopBeats_ = beats;
}

void LaunchHandle::nudge(double beats, SampleDuration samples) {
    // The origin moves, not the end: the played length is what a source reads
    // its position from, so shifting where the run began is what moves the
    // playhead through the material without interrupting the run.
    if (!run_)
        return;

    // Backwards only as far as the run has got, each axis by its own length.
    // Further would put the origin in the future.
    const auto effectiveBeats = std::max(beats, -run_->elapsedBeats);
    const auto effectiveSamples =
        std::max(samples, SampleDuration{-(run_->through - run_->origin).samples});

    run_->originBeat -= effectiveBeats;
    run_->originTimelineBeat -= effectiveBeats;
    run_->elapsedBeats += effectiveBeats;
    run_->origin = run_->origin - effectiveSamples;

    // The schedule moves with the run: a loop counts from where the run now
    // begins, or a nudge would leave the wraps where the un-nudged run put them.
    run_->scheduleBeat -= effectiveBeats;
}

std::optional<BeatRange> LaunchHandle::playedRange() const {
    if (!run_)
        return {};

    return BeatRange{run_->originTimelineBeat, run_->originTimelineBeat + run_->elapsedBeats};
}

std::optional<BeatRange> LaunchHandle::playedMonotonicRange() const {
    if (!run_)
        return {};

    return BeatRange{run_->originBeat, run_->originBeat + run_->elapsedBeats};
}

std::optional<SampleRange> LaunchHandle::playedSampleRange() const {
    if (!run_)
        return {};

    return SampleRange{run_->origin, run_->through};
}

std::optional<BeatRange> LaunchHandle::lastPlayedRange() const {
    return lastPlayed_;
}

std::optional<double> LaunchHandle::scheduleBeat() const {
    if (!run_)
        return {};

    return run_->scheduleBeat;
}

void LaunchHandle::releaseSection() {
    // A request, like every other way a slot stops: a run ended behind the
    // sources' backs is a step nobody can ramp. The stop and the hand-back are
    // one request, so a launch arriving before the next block replaces both.
    stop(std::nullopt);
    pending_->releasesSection = true;
}

void LaunchHandle::beginRun(const SyncRange& range, const BlockInstant& at, double scheduledBeat) {
    if (const auto played = playedRange())
        lastPlayed_ = played;

    playState_ = PlayState::playing;

    // At the sample the run begins on, not the one it was asked on, so a launch
    // quantized to the next bar leaves the arrangement playing until the bar.
    holdsSection_ = true;

    Run run;
    run.origin = at.monotonic;
    run.through = at.monotonic;
    run.originBeat = range.monotonicBeatAt(at);
    run.originTimelineBeat = range.timelineBeatAt(at);

    // The beat that was asked for, not the one the sample landed on. Everything
    // a run sounds is measured from the sample; everything it schedules is
    // measured from here (Run::scheduleBeat).
    run.scheduleBeat = scheduledBeat;
    run.sampleRate = range.rate();

    run_ = run;
}

void LaunchHandle::joinRun(const SyncRange& range, const BlockInstant& at, const Run& other) {
    if (const auto played = playedRange())
        lastPlayed_ = played;

    playState_ = PlayState::playing;
    holdsSection_ = true;

    // Adopting the position, not just the origin. Derived here rather than at
    // the request, which is a different number whenever the launch was queued
    // for a later beat.
    auto run = other;
    run.through = at.monotonic;
    run.elapsedBeats = range.monotonicBeatAt(at) - other.originBeat;

    run_ = run;
}

void LaunchHandle::endRun() {
    if (const auto played = playedRange())
        lastPlayed_ = played;

    playState_ = PlayState::stopped;
    run_.reset();
}

void LaunchHandle::extendRun(const SyncRange& piece) {
    if (!run_)
        return;

    // A length rather than an endpoint. The timeline goes backwards every time
    // the loop wraps and the played range may not, so a run that assigned the
    // block's end beat would shrink at every wrap.
    run_->elapsedBeats += piece.monotonic.length();

    // The far end of the sample axis is an endpoint, and can be: nothing takes
    // it back.
    run_->through = piece.monotonicSamples.end;
}

void LaunchHandle::applyEvent(const SyncRange& range, bool fromPending, const BlockInstant& at,
                              double scheduledBeat) {
    if (!fromPending) {
        // A loop re-trigger, which is a relaunch at the wrap: same handle, new
        // run, so the played range restarts and whatever reads it seeks.
        //
        // The schedule carries over rather than restarting with it. Re-anchoring
        // it to the wrap that just fired is what makes the next interval start
        // from a rounded beat, and that error accumulates (Run::scheduleBeat).
        const auto schedule = run_ ? run_->scheduleBeat : scheduledBeat;
        beginRun(range, at, schedule);
        return;
    }

    const auto pending = *pending_;
    pending_.reset();

    if (pending.state == QueueState::stopQueued) {
        endRun();

        // Where the stop does: a slot cannot give a track up on one sample and
        // fall silent on another.
        if (pending.releasesSection)
            holdsSection_ = false;

        return;
    }

    if (!pending.synced) {
        beginRun(range, at, scheduledBeat);
        return;
    }

    joinRun(range, at, *pending.synced);
}

SplitStatus LaunchHandle::advance(const SyncRange& range) {
    blockStatus_ = advanceOver(range);
    return blockStatus_;
}

SplitStatus LaunchHandle::advanceOver(const SyncRange& range) {
    followRate(range);

    SplitStatus status;
    status.beforeEvent.range = range.timeline;

    // Before an event below can change either: a stop on the first sample
    // leaves no first half to read them off afterwards.
    status.soundingAtStart = playState_ == PlayState::playing;
    status.heldSectionAtStart = holdsSection_;

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

        // Here because applyEvent consumes the request.
        status.releasedSection =
            fromPending && pending_->state == QueueState::stopQueued && pending_->releasesSection;
    }

    if (!eventBeat && playState_ == PlayState::playing && loopBeats_ && run_) {
        // From the beat the run was scheduled for rather than the one it
        // sounded on, which is what keeps a thousand wraps exact (#2336).
        const auto origin = run_->scheduleBeat;
        const auto elapsed = range.monotonic.start - origin;

        // The first wrap at or after this block begins, which is ceil rather
        // than floor-plus-one: a wrap due exactly on the first sample was
        // passed over by the previous block, whose range ended before it, and
        // rounding up again here would skip it a second time and every
        // block-aligned wrap after it. Never the origin itself, which is where
        // the run started rather than somewhere it repeats.
        const auto turns = std::max(1.0, std::ceil(elapsed / *loopBeats_));
        auto wrap = origin + (turns * *loopBeats_);

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
            status.beforeEvent.origin =
                MaterialOrigin{virtualStart(range), virtualStartSeconds(range)};
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
        applyEvent(range, fromPending, range.start(), *eventBeat);

        if (playState_ == PlayState::playing) {
            status.beforeEvent.origin =
                MaterialOrigin{virtualStart(range), virtualStartSeconds(range)};
            extendRun(range);
        }

        return status;
    }

    const auto first = range.upTo(event);
    const auto second = range.from(event);

    status.beforeEvent.range = first.timeline;

    if (playState_ == PlayState::playing) {
        status.beforeEvent.origin = MaterialOrigin{virtualStart(first), virtualStartSeconds(first)};
        extendRun(first);
    }

    applyEvent(range, fromPending, event, *eventBeat);

    BlockPiece after;
    after.range = second.timeline;

    if (playState_ == PlayState::playing) {
        after.origin = MaterialOrigin{virtualStart(second), virtualStartSeconds(second)};
        extendRun(second);
    }

    status.afterEvent = after;
    return status;
}

}  // namespace magda::engine
