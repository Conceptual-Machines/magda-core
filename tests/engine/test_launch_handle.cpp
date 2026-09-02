#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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

/// Half a second a beat, which is 120 bpm and the tempo every case here runs
/// at unless it says otherwise.
constexpr double kSecondsPerBeat = 0.5;

/// One block, in all four faces, with the timeline and the monotonic clocks
/// running together. The two only diverge when something wraps the timeline,
/// which the loop cases below do on purpose.
SyncRange block(double from, double to) {
    return SyncRange{BeatRange{from, to}, BeatRange{from, to},
                     SecondsRange{from * kSecondsPerBeat, to * kSecondsPerBeat},
                     SecondsRange{from * kSecondsPerBeat, to * kSecondsPerBeat}};
}

/// A block the timeline has wrapped under: it covers @p from to @p to on the
/// timeline and @p monotonicFrom to @p monotonicTo on the clocks that do not
/// go back.
SyncRange wrapped(double from, double to, double monotonicFrom, double monotonicTo) {
    return SyncRange{BeatRange{from, to}, BeatRange{monotonicFrom, monotonicTo},
                     SecondsRange{from * kSecondsPerBeat, to * kSecondsPerBeat},
                     SecondsRange{monotonicFrom * kSecondsPerBeat, monotonicTo * kSecondsPerBeat}};
}

}  // namespace

TEST_CASE("A handle starts stopped and having played nothing", "[launch]") {
    LaunchHandle handle;

    CHECK(handle.playState() == LaunchHandle::PlayState::stopped);
    CHECK_FALSE(handle.queuedState().has_value());
    CHECK_FALSE(handle.playedRange().has_value());

    const auto status = handle.advance(block(0.0, 1.0));

    CHECK_FALSE(status.isSplit);
    CHECK_FALSE(status.playing1);
}

TEST_CASE("An unquantized launch takes the whole block", "[launch]") {
    LaunchHandle handle;
    handle.play({});

    CHECK(handle.queuedState() == LaunchHandle::QueueState::playQueued);

    const auto status = handle.advance(block(0.0, 1.0));

    CHECK_FALSE(status.isSplit);
    CHECK(status.playing1);
    CHECK(status.range1 == BeatRange{0.0, 1.0});
    CHECK(status.origin1->beat == Approx(0.0));
    CHECK(handle.playState() == LaunchHandle::PlayState::playing);
    CHECK_FALSE(handle.queuedState().has_value());
}

TEST_CASE("A launch quantized inside a block splits it at the beat", "[launch]") {
    LaunchHandle handle;
    handle.play(2.5);

    // Nothing yet: the block ends before the launch beat.
    const auto before = handle.advance(block(0.0, 2.0));
    CHECK_FALSE(before.isSplit);
    CHECK_FALSE(before.playing1);
    CHECK(handle.queuedState() == LaunchHandle::QueueState::playQueued);

    const auto status = handle.advance(block(2.0, 3.0));

    REQUIRE(status.isSplit);
    CHECK_FALSE(status.playing1);
    CHECK(status.playing2);
    CHECK(status.range1 == BeatRange{2.0, 2.5});
    CHECK(status.range2 == BeatRange{2.5, 3.0});
    CHECK_FALSE(status.origin1.has_value());
    REQUIRE(status.origin2.has_value());
    CHECK(status.origin2->beat == Approx(2.5));
}

TEST_CASE("A launch on a block boundary is not split and not counted twice", "[launch]") {
    LaunchHandle handle;
    handle.play(2.0);

    const auto first = handle.advance(block(0.0, 2.0));

    // The range is half open, so beat 2.0 belongs to the next block and this
    // one does not launch.
    CHECK_FALSE(first.isSplit);
    CHECK_FALSE(first.playing1);

    const auto second = handle.advance(block(2.0, 4.0));

    CHECK_FALSE(second.isSplit);
    CHECK(second.playing1);
    CHECK(second.range1 == BeatRange{2.0, 4.0});
    REQUIRE(handle.playedRange().has_value());
    CHECK(handle.playedRange()->length() == Approx(2.0));
}

TEST_CASE("A quantized stop splits the block it lands in", "[launch]") {
    LaunchHandle handle;
    handle.play({});
    handle.advance(block(0.0, 4.0));

    handle.stop(5.5);
    const auto status = handle.advance(block(4.0, 6.0));

    REQUIRE(status.isSplit);
    CHECK(status.playing1);
    CHECK_FALSE(status.playing2);
    CHECK(status.range1 == BeatRange{4.0, 5.5});
    CHECK(status.range2 == BeatRange{5.5, 6.0});
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

    REQUIRE(status.isSplit);
    CHECK(status.playing2);
    CHECK(status.range2 == BeatRange{4.0, 5.0});
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
    CHECK_FALSE(first.playing1);

    const auto second = handle.advance(wrapped(0.0, 2.0, 8.0, 10.0));

    REQUIRE(second.isSplit);
    CHECK(second.playing2);
    CHECK(second.range2 == BeatRange{1.0, 2.0});
}

TEST_CASE("Looping re-triggers the run at the duration", "[launch]") {
    LaunchHandle handle;
    handle.play({});
    handle.setLooping(2.0);

    handle.advance(block(0.0, 1.0));

    REQUIRE(handle.playedRange().has_value());
    CHECK(handle.playedRange()->length() == Approx(1.0));

    const auto status = handle.advance(block(1.0, 3.0));

    REQUIRE(status.isSplit);
    CHECK(status.playing1);
    CHECK(status.playing2);
    CHECK(status.range1 == BeatRange{1.0, 2.0});
    CHECK(status.range2 == BeatRange{2.0, 3.0});

    // The run restarted at the wrap rather than carrying on through it.
    REQUIRE(status.origin2.has_value());
    CHECK(status.origin2->beat == Approx(2.0));
    CHECK(handle.playedRange()->length() == Approx(1.0));
}

TEST_CASE("A queued stop beats a loop wrap later in the same block", "[launch]") {
    LaunchHandle handle;
    handle.play({});
    handle.setLooping(4.0);
    handle.advance(block(0.0, 3.0));

    handle.stop(3.5);
    const auto status = handle.advance(block(3.0, 5.0));

    REQUIRE(status.isSplit);
    CHECK(status.playing1);
    CHECK_FALSE(status.playing2);
    CHECK(status.range1 == BeatRange{3.0, 3.5});
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

    REQUIRE(statusA.isSplit);
    REQUIRE(statusB.isSplit);
    CHECK(statusA.range2.start == Approx(statusB.range2.start));
    CHECK(statusA.origin2->beat == Approx(statusB.origin2->beat));
}

TEST_CASE("Nudge moves the playhead without ending the run", "[launch]") {
    LaunchHandle handle;
    handle.play({});
    handle.advance(block(0.0, 2.0));

    handle.nudge(0.5, 0.5 * kSecondsPerBeat);

    REQUIRE(handle.playedRange().has_value());
    CHECK(handle.playedRange()->length() == Approx(2.5));
    CHECK(handle.playState() == LaunchHandle::PlayState::playing);
}

TEST_CASE("A stopped handle advancing changes nothing", "[launch]") {
    LaunchHandle handle;

    for (auto beat = 0.0; beat < 8.0; beat += 1.0) {
        const auto status = handle.advance(block(beat, beat + 1.0));
        CHECK_FALSE(status.playing1);
        CHECK_FALSE(status.isSplit);
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

    REQUIRE(status.isSplit);
    CHECK(status.playing1);
    CHECK_FALSE(status.playing2);
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

    REQUIRE(status.isSplit);
    CHECK(status.playing1);
    CHECK_FALSE(status.playing2);
    CHECK(handle.playState() == LaunchHandle::PlayState::stopped);

    // On the bar, to the sample, rather than a block late.
    CHECK(status.range2.start == Approx(12.0));
}

TEST_CASE("A re-trigger lands on its own beat, not on the wrap before it", "[launch]") {
    LaunchHandle handle;
    handle.play(3.99);
    handle.setLooping(8.0);
    handle.advance(block(3.99, 11.98));

    handle.play(12.0);
    const auto status = handle.advance(block(11.98, 12.01));

    REQUIRE(status.isSplit);
    CHECK(status.playing2);

    // The run's origin is the beat that was asked for. The wrap at 11.99 would
    // have put it there 0.01 beats early and kept the drift alive.
    REQUIRE(status.origin2.has_value());
    CHECK(status.origin2->beat == Approx(12.0));
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

    REQUIRE(status.isSplit);
    CHECK(status.range2.start == Approx(0.011));
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
    CHECK_FALSE(before.isSplit);
    CHECK(handle.playedRange()->length() == Approx(2.0));

    const auto status = handle.advance(block(2.0, 3.0));

    // No split: the wrap is on the first sample, so the whole block belongs to
    // the new run and a split would report an empty half.
    CHECK_FALSE(status.isSplit);
    CHECK(status.playing1);
    REQUIRE(status.origin1.has_value());
    CHECK(status.origin1->beat == Approx(2.0));
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
        handle.nudge(-0.5, -0.5 * kSecondsPerBeat);
        CHECK(handle.playedRange()->length() == Approx(1.5));
    }

    SECTION("past the start it clamps rather than inverting") {
        handle.nudge(-3.0, -3.0 * kSecondsPerBeat);

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

    REQUIRE(status.origin1.has_value());

    // What a source actually computes: where it is in the material. Two beats
    // in, because that is how long the run has been going.
    CHECK(status.range1.start - status.origin1->beat == Approx(2.0));
    CHECK(status.origin1->beat == Approx(-2.0));
}
