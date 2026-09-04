#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <vector>

#include "launch/LaunchHandle.hpp"

/**
 * @file test_launch_handle.cpp
 * @brief The launcher's state machine, driven by hand (#2300).
 *
 * Every case here is a sequence of blocks fed to a handle, which is what makes
 * this the one part of #1894 that is fully deterministic offline. Nothing is
 * rendered and nothing is compared against the fork: what is asserted is where
 * a launch lands and what the handle says it has played, both of which are
 * numbers rather than audio.
 */

using namespace magda::engine;
using Catch::Approx;

namespace {

/// 120 bpm, the tempo every case here runs at.
constexpr double kSecondsPerBeat = 0.5;

constexpr double kSampleRate = 48000.0;

/// How many samples a stretch of beats is at that tempo. A block carries its
/// sample count because an instant inside it is named in samples: that is the
/// one coordinate every other is derived from (#2330).
constexpr int samplesFor(double beats) {
    return static_cast<int>(beats * kSecondsPerBeat * kSampleRate);
}

/// Where a monotonic beat sits on the transport's count, at that tempo. The
/// count is what a run's origin is recorded as, so a block carries it (#2336).
SamplePosition at(double monotonicBeat) {
    return SamplePosition{static_cast<std::int64_t>(monotonicBeat * kSecondsPerBeat * kSampleRate)};
}

/// One block, with the timeline and monotonic clocks running together. They
/// diverge only when something wraps the timeline.
SyncRange block(double from, double to) {
    return SyncRange{BeatRange{from, to},
                     BeatRange{from, to},
                     SecondsRange{from * kSecondsPerBeat, to * kSecondsPerBeat},
                     SampleRange{at(from), at(to)},
                     samplesFor(to - from),
                     kSampleRate};
}

/// A block the timeline has wrapped under.
SyncRange wrapped(double from, double to, double monotonicFrom, double monotonicTo) {
    return SyncRange{BeatRange{from, to},
                     BeatRange{monotonicFrom, monotonicTo},
                     SecondsRange{from * kSecondsPerBeat, to * kSecondsPerBeat},
                     SampleRange{at(monotonicFrom), at(monotonicTo)},
                     samplesFor(monotonicTo - monotonicFrom),
                     kSampleRate};
}

}  // namespace

TEST_CASE("An event in the block's last half sample stays inside the block", "[launch]") {
    // A beat nearer the next callback's first sample than this one's last is
    // where nearest would answer past the end of the block. Applied there, the
    // event happens but nothing can carry it: a stop would clear its own note
    // state while its note-offs went to an offset outside the buffer, and the
    // notes would hang. Floor answers the sample the beat is inside, which is
    // one this block has (TimeDomains::eventAt).
    LaunchHandle handle;

    const auto range = block(0.0, 1.0);
    const auto lastSample = range.numSamples - 1;

    // Inside the beat range, and within half a sample of its end.
    handle.play(1.0 - (0.4 / range.numSamples));

    const auto status = handle.advance(range);

    REQUIRE(status.afterEvent.has_value());

    // On the block's last sample: at most one sample early, and a sample that
    // plays. Never numSamples, which belongs to the next callback.
    CHECK(status.event.sample == lastSample);
    CHECK(status.event.sample < range.numSamples);
}

TEST_CASE("A handle starts stopped and having played nothing", "[launch]") {
    LaunchHandle handle;

    CHECK(handle.playState() == LaunchHandle::PlayState::stopped);
    CHECK_FALSE(handle.queuedState().has_value());
    CHECK_FALSE(handle.playedRange().has_value());

    const auto status = handle.advance(block(0.0, 1.0));

    CHECK_FALSE(status.afterEvent.has_value());
    CHECK_FALSE(status.beforeEvent.playing());
}

TEST_CASE("An unquantized launch takes the whole block", "[launch]") {
    LaunchHandle handle;
    handle.play({});

    CHECK(handle.queuedState() == LaunchHandle::QueueState::playQueued);

    const auto status = handle.advance(block(0.0, 1.0));

    CHECK_FALSE(status.afterEvent.has_value());
    CHECK(status.beforeEvent.playing());
    CHECK(status.beforeEvent.range == BeatRange{0.0, 1.0});
    CHECK(status.beforeEvent.origin->beat == Approx(0.0));
    CHECK(handle.playState() == LaunchHandle::PlayState::playing);
    CHECK_FALSE(handle.queuedState().has_value());
}

TEST_CASE("A launch quantized inside a block splits it at the beat", "[launch]") {
    LaunchHandle handle;
    handle.play(2.5);

    // Nothing yet: the block ends before the launch beat.
    const auto before = handle.advance(block(0.0, 2.0));
    CHECK_FALSE(before.afterEvent.has_value());
    CHECK_FALSE(before.beforeEvent.playing());
    CHECK(handle.queuedState() == LaunchHandle::QueueState::playQueued);

    const auto status = handle.advance(block(2.0, 3.0));

    REQUIRE(status.afterEvent.has_value());
    CHECK_FALSE(status.beforeEvent.playing());
    CHECK(status.afterEvent->playing());
    CHECK(status.beforeEvent.range == BeatRange{2.0, 2.5});
    CHECK(status.afterEvent->range == BeatRange{2.5, 3.0});
    CHECK(status.afterEvent->origin->beat == Approx(2.5));
}

TEST_CASE("A launch on a block boundary is not split and not counted twice", "[launch]") {
    LaunchHandle handle;
    handle.play(2.0);

    const auto first = handle.advance(block(0.0, 2.0));

    // The range is half open, so beat 2.0 belongs to the next block and this
    // one does not launch.
    CHECK_FALSE(first.afterEvent.has_value());
    CHECK_FALSE(first.beforeEvent.playing());

    const auto second = handle.advance(block(2.0, 4.0));

    CHECK_FALSE(second.afterEvent.has_value());
    CHECK(second.beforeEvent.playing());
    CHECK(second.beforeEvent.range == BeatRange{2.0, 4.0});
    REQUIRE(handle.playedRange().has_value());
    CHECK(handle.playedRange()->length() == Approx(2.0));
}

TEST_CASE("A quantized stop splits the block it lands in", "[launch]") {
    LaunchHandle handle;
    handle.play({});
    handle.advance(block(0.0, 4.0));

    handle.stop(5.5);
    const auto status = handle.advance(block(4.0, 6.0));

    REQUIRE(status.afterEvent.has_value());
    CHECK(status.beforeEvent.playing());
    CHECK_FALSE(status.afterEvent->playing());
    CHECK(status.beforeEvent.range == BeatRange{4.0, 5.5});
    CHECK(status.afterEvent->range == BeatRange{5.5, 6.0});
    CHECK(handle.playState() == LaunchHandle::PlayState::stopped);
    CHECK_FALSE(handle.playedRange().has_value());

    REQUIRE(handle.lastPlayedRange().has_value());
    CHECK(handle.lastPlayedRange()->length() == Approx(5.5));
}

TEST_CASE("A relaunch does not carry the previous run's length", "[launch]") {
    LaunchHandle handle;

    handle.play({});
    handle.advance(block(0.0, 4.0));
    handle.stop({});
    handle.advance(block(4.0, 5.0));

    handle.play({});
    handle.advance(block(5.0, 6.0));

    REQUIRE(handle.playedRange().has_value());
    CHECK(handle.playedRange()->start == Approx(5.0));
    CHECK(handle.playedRange()->length() == Approx(1.0));
}

TEST_CASE("A later request replaces one that has not fired", "[launch]") {
    LaunchHandle handle;

    handle.play(8.0);
    handle.play(4.0);

    CHECK(handle.queuedPosition() == Approx(4.0));

    const auto status = handle.advance(block(3.0, 5.0));

    REQUIRE(status.afterEvent.has_value());
    CHECK(status.afterEvent->playing());
    CHECK(status.afterEvent->range == BeatRange{4.0, 5.0});
}

TEST_CASE("The played range keeps increasing across a timeline wrap", "[launch]") {
    LaunchHandle handle;
    handle.play({});

    // Two bars, then the loop takes the timeline back to zero while the
    // monotonic clock carries on. This is the case the two domains exist for.
    handle.advance(wrapped(6.0, 8.0, 6.0, 8.0));
    handle.advance(wrapped(0.0, 2.0, 8.0, 10.0));

    REQUIRE(handle.playedRange().has_value());
    CHECK(handle.playedRange()->start == Approx(6.0));
    CHECK(handle.playedRange()->length() == Approx(4.0));

    REQUIRE(handle.playedMonotonicRange().has_value());
    CHECK(handle.playedMonotonicRange()->length() == Approx(4.0));
}

TEST_CASE("A queued launch fires on the monotonic beat, not the timeline one", "[launch]") {
    LaunchHandle handle;
    handle.play(9.0);

    // The timeline visits beat 9.0 in neither block, and the launch still
    // happens: a queued position is a monotonic one. A handle that resolved
    // against the timeline would wait for ever here.
    const auto first = handle.advance(wrapped(6.0, 8.0, 6.0, 8.0));
    CHECK_FALSE(first.beforeEvent.playing());

    const auto second = handle.advance(wrapped(0.0, 2.0, 8.0, 10.0));

    REQUIRE(second.afterEvent.has_value());
    CHECK(second.afterEvent->playing());
    CHECK(second.afterEvent->range == BeatRange{1.0, 2.0});
}

TEST_CASE("Looping re-triggers the run at the duration", "[launch]") {
    LaunchHandle handle;
    handle.play({});
    handle.setLooping(2.0);

    handle.advance(block(0.0, 1.0));

    REQUIRE(handle.playedRange().has_value());
    CHECK(handle.playedRange()->length() == Approx(1.0));

    const auto status = handle.advance(block(1.0, 3.0));

    REQUIRE(status.afterEvent.has_value());
    CHECK(status.beforeEvent.playing());
    CHECK(status.afterEvent->playing());
    CHECK(status.beforeEvent.range == BeatRange{1.0, 2.0});
    CHECK(status.afterEvent->range == BeatRange{2.0, 3.0});

    // The run restarted at the wrap rather than carrying on through it.
    REQUIRE(status.afterEvent->origin.has_value());
    CHECK(status.afterEvent->origin->beat == Approx(2.0));
    CHECK(handle.playedRange()->length() == Approx(1.0));
}

TEST_CASE("A queued stop beats a loop wrap later in the same block", "[launch]") {
    LaunchHandle handle;
    handle.play({});
    handle.setLooping(4.0);
    handle.advance(block(0.0, 3.0));

    handle.stop(3.5);
    const auto status = handle.advance(block(3.0, 5.0));

    REQUIRE(status.afterEvent.has_value());
    CHECK(status.beforeEvent.playing());
    CHECK_FALSE(status.afterEvent->playing());
    CHECK(status.beforeEvent.range == BeatRange{3.0, 3.5});
    CHECK(handle.playState() == LaunchHandle::PlayState::stopped);
}

TEST_CASE("playSynced joins a run in progress rather than starting one", "[launch]") {
    LaunchHandle leader;
    leader.play({});
    leader.advance(block(0.0, 4.0));

    LaunchHandle follower;
    follower.playSynced(leader, {});
    follower.advance(block(4.0, 6.0));
    leader.advance(block(4.0, 6.0));

    REQUIRE(follower.playedRange().has_value());
    REQUIRE(leader.playedRange().has_value());

    // Same origin and same elapsed length: the two are in phase, which is what
    // a scene launch has to be.
    CHECK(follower.playedRange()->start == Approx(leader.playedRange()->start));
    CHECK(follower.playedRange()->length() == Approx(leader.playedRange()->length()));
}

TEST_CASE("A scene launch starts every handle on the same beat", "[launch]") {
    LaunchHandle a;
    LaunchHandle b;

    a.play(4.0);
    b.play(4.0);

    const auto statusA = a.advance(block(3.0, 5.0));
    const auto statusB = b.advance(block(3.0, 5.0));

    REQUIRE(statusA.afterEvent.has_value());
    REQUIRE(statusB.afterEvent.has_value());
    CHECK(statusA.afterEvent->range.start == Approx(statusB.afterEvent->range.start));
    CHECK(statusA.afterEvent->origin->beat == Approx(statusB.afterEvent->origin->beat));
}

TEST_CASE("Nudge moves the playhead without ending the run", "[launch]") {
    LaunchHandle handle;
    handle.play({});
    handle.advance(block(0.0, 2.0));

    handle.nudge(0.5, SampleDuration::ofSeconds(0.5 * kSecondsPerBeat, kSampleRate));

    REQUIRE(handle.playedRange().has_value());
    CHECK(handle.playedRange()->length() == Approx(2.5));
    CHECK(handle.playState() == LaunchHandle::PlayState::playing);
}

TEST_CASE("A stopped handle advancing changes nothing", "[launch]") {
    LaunchHandle handle;

    for (auto beat = 0.0; beat < 8.0; beat += 1.0) {
        const auto status = handle.advance(block(beat, beat + 1.0));
        CHECK_FALSE(status.beforeEvent.playing());
        CHECK_FALSE(status.afterEvent.has_value());
    }

    CHECK_FALSE(handle.playedRange().has_value());
    CHECK_FALSE(handle.lastPlayedRange().has_value());
}

TEST_CASE("A request and a loop wrap on the same beat resolve to the request", "[launch]") {
    // The case that actually happens: a clip looping every four bars, stopped
    // on the grid, so the stop is quantized to the very beat the wrap lands
    // on. Both are due at once and the request is what the person asked for.
    LaunchHandle handle;
    handle.play({});
    handle.setLooping(4.0);
    handle.advance(block(0.0, 3.9));

    handle.stop(4.0);
    const auto status = handle.advance(block(3.9, 4.1));

    REQUIRE(status.afterEvent.has_value());
    CHECK(status.beforeEvent.playing());
    CHECK_FALSE(status.afterEvent->playing());
    CHECK(handle.playState() == LaunchHandle::PlayState::stopped);

    // And the wrap is moot rather than deferred: there is no run left to
    // restart, so nothing is owed to the next block.
    CHECK_FALSE(handle.queuedState().has_value());
}

TEST_CASE("A wrap never displaces a quantized request", "[launch]") {
    // The drift dies with the unquantized launch that caused it rather than
    // being carried into an action somebody explicitly put on the grid.
    LaunchHandle handle;
    handle.play(3.99);  // off the grid, so the wraps are off the grid too
    handle.setLooping(8.0);
    handle.advance(block(3.99, 11.98));

    // The wrap is due at 11.99 and the stop at 12.0, both inside this block.
    // The wrap is earlier and still gives way.
    handle.stop(12.0);
    const auto status = handle.advance(block(11.98, 12.01));

    REQUIRE(status.afterEvent.has_value());
    CHECK(status.beforeEvent.playing());
    CHECK_FALSE(status.afterEvent->playing());
    CHECK(handle.playState() == LaunchHandle::PlayState::stopped);

    // On the bar, to the sample, rather than a block late.
    CHECK(status.afterEvent->range.start == Approx(12.0));
}

TEST_CASE("A re-trigger lands on its own beat, not on the wrap before it", "[launch]") {
    LaunchHandle handle;
    handle.play(3.99);
    handle.setLooping(8.0);
    handle.advance(block(3.99, 11.98));

    handle.play(12.0);
    const auto status = handle.advance(block(11.98, 12.01));

    REQUIRE(status.afterEvent.has_value());
    CHECK(status.afterEvent->playing());

    // The run's origin is the beat that was asked for. The wrap at 11.99 would
    // have put it there 0.01 beats early and kept the drift alive.
    REQUIRE(status.afterEvent->origin.has_value());
    CHECK(status.afterEvent->origin->beat == Approx(12.0));
    CHECK(handle.playedRange()->start == Approx(12.0));
}

TEST_CASE("A request fires on its beat however often the loop wraps", "[launch]") {
    // A loop short enough to put a wrap in every block cannot hold a request
    // off, because it never outranks one in the first place.
    LaunchHandle handle;
    handle.play({});
    handle.setLooping(0.01);
    handle.advance(block(0.0, 0.005));

    handle.stop(0.011);
    const auto status = handle.advance(block(0.005, 0.03));

    REQUIRE(status.afterEvent.has_value());
    CHECK(status.afterEvent->range.start == Approx(0.011));
    CHECK(handle.playState() == LaunchHandle::PlayState::stopped);
    CHECK_FALSE(handle.queuedState().has_value());
}

TEST_CASE("A loop too short for a block says so rather than stopping quietly", "[launch]") {
    LaunchHandle handle;
    handle.play({});
    handle.setLooping(0.01);

    handle.advance(block(0.0, 0.005));
    CHECK(handle.loopRetriggerOverflows() == 0);

    // Ten durations inside one block, of which one can be reported.
    handle.advance(block(0.005, 0.105));

    CHECK(handle.loopRetriggerOverflows() == 1);

    // Nothing a session clip can reach: durations are bars and a block is
    // milliseconds. A one-beat loop is twenty blocks long and never counts.
    LaunchHandle realistic;
    realistic.play({});
    realistic.setLooping(1.0);
    for (auto beat = 0.0; beat < 8.0; beat += 0.025)
        realistic.advance(block(beat, beat + 0.025));

    CHECK(realistic.loopRetriggerOverflows() == 0);
}

TEST_CASE("A wrap landing on a block's first sample is not skipped twice", "[launch]") {
    // The half-open boundary, from the loop's side. The block ending at the
    // wrap does not contain it, so the block starting there has to, and a
    // handle that rounded up again would skip every block-aligned wrap.
    LaunchHandle handle;
    handle.play({});
    handle.setLooping(2.0);

    const auto before = handle.advance(block(0.0, 2.0));
    CHECK_FALSE(before.afterEvent.has_value());
    CHECK(handle.playedRange()->length() == Approx(2.0));

    const auto status = handle.advance(block(2.0, 3.0));

    // No split: the wrap is on the first sample, so the whole block belongs to
    // the new run and a split would report an empty half.
    CHECK_FALSE(status.afterEvent.has_value());
    CHECK(status.beforeEvent.playing());
    REQUIRE(status.beforeEvent.origin.has_value());
    CHECK(status.beforeEvent.origin->beat == Approx(2.0));
    CHECK(handle.playedRange()->length() == Approx(1.0));

    // And it keeps wrapping rather than having lost the phase.
    handle.advance(block(3.0, 4.0));
    handle.advance(block(4.0, 5.0));
    CHECK(handle.playedRange()->length() == Approx(1.0));
}

TEST_CASE("A backward nudge cannot take a run before it started", "[launch]") {
    LaunchHandle handle;
    handle.play({});
    handle.advance(block(0.0, 2.0));

    SECTION("within the run it shifts the origin") {
        handle.nudge(-0.5, SampleDuration::ofSeconds(-0.5 * kSecondsPerBeat, kSampleRate));
        CHECK(handle.playedRange()->length() == Approx(1.5));
    }

    SECTION("past the start it clamps rather than inverting") {
        handle.nudge(-3.0, SampleDuration::ofSeconds(-3.0 * kSecondsPerBeat, kSampleRate));

        const auto played = *handle.playedRange();
        CHECK(played.length() == Approx(0.0));
        CHECK(played.start <= played.end);
        CHECK(handle.playState() == LaunchHandle::PlayState::playing);

        // The loop origin came with it: a run of zero length whose next wrap
        // was already behind it would fire immediately and for ever.
        CHECK(handle.playedMonotonicRange()->length() == Approx(0.0));
    }
}

TEST_CASE("playStartTime survives the timeline wrapping under it", "[launch]") {
    LaunchHandle handle;
    handle.play({});
    handle.advance(wrapped(6.0, 8.0, 6.0, 8.0));

    // The loop takes the timeline back to zero. The run began at beat 6, which
    // is no longer a beat in the cycle this block belongs to.
    const auto status = handle.advance(wrapped(0.0, 2.0, 8.0, 10.0));

    REQUIRE(status.beforeEvent.origin.has_value());

    // What a source actually computes: where it is in the material. Two beats
    // in, because that is how long the run has been going.
    CHECK(status.beforeEvent.range.start - status.beforeEvent.origin->beat == Approx(2.0));
    CHECK(status.beforeEvent.origin->beat == Approx(-2.0));
}

TEST_CASE("A thousand retriggers do not walk off the beat", "[launch][2336]") {
    // The drift the epic opens with. At 48 kHz and 123 bpm a beat is 23,414.634
    // samples, so no whole number of samples is a beat, and a wrap scheduled
    // from the sample the previous wrap landed on rounds up every time: after a
    // thousand of them the run is about 366 samples late, a third of a beat
    // short of eight milliseconds.
    //
    // What stops it is that the schedule and the sound are different questions.
    // A wrap sounds on a sample, because that is the only thing that can sound;
    // the next wrap is counted from the beat the run was asked for, which no
    // rounding has ever touched (LaunchHandle::Run::scheduleBeat).
    constexpr auto kBpm = 123.0;
    constexpr auto kRate = 48000.0;
    constexpr auto kBlock = 512;
    constexpr auto kTurns = 1000;

    const TempoMap tempo({{0.0, kBpm, 0.0f}}, {});
    const auto samplesPerBeat = (60.0 / kBpm) * kRate;

    const auto blockFrom = [&](std::int64_t sample) {
        const auto from = static_cast<double>(sample) / kRate;
        const auto to = static_cast<double>(sample + kBlock) / kRate;

        return SyncRange{BeatRange{tempo.timeToBeat(from), tempo.timeToBeat(to)},
                         BeatRange{tempo.timeToBeat(from), tempo.timeToBeat(to)},
                         SecondsRange{from, to},
                         SampleRange{SamplePosition{sample}, SamplePosition{sample + kBlock}},
                         kBlock,
                         kRate,
                         &tempo};
    };

    LaunchHandle handle;
    handle.play({});
    handle.setLooping(1.0);

    std::vector<std::int64_t> wraps;
    auto origin = SamplePosition{0};

    // A beat past the last wrap that is due, so the thousandth has somewhere to
    // land and nothing after it is counted.
    const auto blocks = static_cast<std::int64_t>(((kTurns + 1) * samplesPerBeat) / kBlock);

    for (std::int64_t i = 0; i < blocks; ++i) {
        handle.advance(blockFrom(i * kBlock));

        const auto run = handle.playedSampleRange();
        REQUIRE(run.has_value());

        if (run->start != origin) {
            origin = run->start;
            wraps.push_back(origin.sample);
        }
    }

    REQUIRE(wraps.size() >= static_cast<std::size_t>(kTurns));

    for (std::size_t turn = 0; turn < static_cast<std::size_t>(kTurns); ++turn) {
        // Where the beat is, rather than where a chain of rounded intervals
        // would have got to. Within a sample at the thousandth as at the first.
        const auto expected = static_cast<double>(turn + 1) * samplesPerBeat;
        CHECK(std::abs(static_cast<double>(wraps[turn]) - expected) <= 1.0);
    }
}

TEST_CASE("A rate change does not reinterpret a run's origin", "[launch][2336]") {
    // A run's origin is a count of samples, and a device that changes rate
    // changes what a sample is worth. Holding the number would silently move
    // the run: a clip a second into its file would be half a second in, or two,
    // depending on which way the rate went.
    //
    // What is kept is the time the run has actually had. The origin is
    // re-counted behind the block at the new rate, which is the only thing that
    // leaves the material where the listener last heard it.
    LaunchHandle handle;
    handle.play({});
    handle.advance(block(0.0, 2.0));  // one second at 120 bpm

    // The same transport count, carrying on: what changes is what each sample
    // is worth, not how many have gone by.
    const auto doubled = SyncRange{BeatRange{2.0, 3.0},
                                   BeatRange{2.0, 3.0},
                                   SecondsRange{1.0, 1.5},
                                   SampleRange{SamplePosition{48000}, SamplePosition{96000}},
                                   48000,
                                   2.0 * kSampleRate};

    const auto status = handle.advance(doubled);

    REQUIRE(status.beforeEvent.origin.has_value());

    // Still one second in, so the run's material begins where the block does
    // minus the second it has had. Without the re-count it would read half a
    // second, and every launched clip would jump backwards through its file.
    CHECK(status.beforeEvent.origin->seconds == Approx(0.0));
    CHECK(doubled.seconds.start - status.beforeEvent.origin->seconds == Approx(1.0));
}

TEST_CASE("A run's origin is the same sample after the timeline wraps", "[launch][2336]") {
    // The durable identity. The beat the run began on stops being a beat in
    // any cycle the moment the loop takes it back, and the second it began on
    // is one the transport has passed twice; the sample it began on is the
    // sample it began on.
    LaunchHandle handle;
    handle.play({});
    handle.advance(wrapped(6.0, 8.0, 6.0, 8.0));

    REQUIRE(handle.playedSampleRange().has_value());
    const auto origin = handle.playedSampleRange()->start;

    handle.advance(wrapped(0.0, 2.0, 8.0, 10.0));
    handle.advance(wrapped(2.0, 4.0, 10.0, 12.0));

    CHECK(handle.playedSampleRange()->start == origin);

    // And the run has got as far as the count says, whatever the cursor did.
    CHECK(handle.playedSampleRange()->length() == SampleDuration{samplesFor(6.0)});
}

TEST_CASE("A synced launch joins the run as the rate left it", "[launch][2336]") {
    // A scene launch is a request now and a launch at the quantized beat, and
    // between the two the device can change rate. The run to join is held as a
    // copy, and a copy counted in samples of one length adopted at another puts
    // the follower somewhere the leader is not: at 44.1 to 48 kHz with a second
    // in between it is about 90 ms, for the rest of the run.
    //
    // So the copy follows the rate on the same blocks the leader's own run
    // does, which is what makes joining at the launch instant and joining a
    // copy the same answer.
    LaunchHandle leader;
    LaunchHandle follower;

    leader.play({});
    leader.advance(block(0.0, 2.0));  // one second at 48 kHz

    // Queued for a beat that has not arrived, which is what a scene launch is.
    follower.playSynced(leader, 4.0);

    // The device doubles its rate. The transport's count carries straight on;
    // what changes is what each sample is worth.
    const auto doubled = [](double from, double to, std::int64_t fromSample) {
        return SyncRange{BeatRange{from, to},
                         BeatRange{from, to},
                         SecondsRange{from * kSecondsPerBeat, to * kSecondsPerBeat},
                         SampleRange{SamplePosition{fromSample},
                                     SamplePosition{fromSample + static_cast<std::int64_t>(
                                                                     (to - from) * kSecondsPerBeat *
                                                                     2.0 * kSampleRate)}},
                         static_cast<int>((to - from) * kSecondsPerBeat * 2.0 * kSampleRate),
                         2.0 * kSampleRate};
    };

    const auto second = doubled(2.0, 3.0, 48000);
    leader.advance(second);
    follower.advance(second);

    // And now the launch beat arrives.
    const auto third = doubled(3.0, 5.0, 96000);
    const auto leading = leader.advance(third);
    const auto following = follower.advance(third);

    REQUIRE(follower.playedSampleRange().has_value());
    REQUIRE(leader.playedSampleRange().has_value());

    // The same origin, which is the whole point of a synced launch: the same
    // sample, not merely the same bar.
    CHECK(follower.playedSampleRange()->start == leader.playedSampleRange()->start);

    REQUIRE(following.afterEvent.has_value());
    REQUIRE(following.afterEvent->origin.has_value());
    REQUIRE(leading.beforeEvent.origin.has_value());

    // And what a source is handed says the same thing on both axes. The origin
    // is where the run began in absolute terms, so the two agree whatever piece
    // of the block each was worked out over.
    CHECK(following.afterEvent->origin->beat == Approx(leading.beforeEvent.origin->beat));
    CHECK(following.afterEvent->origin->seconds == Approx(leading.beforeEvent.origin->seconds));
}
