#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <vector>

#include "magda/daw/audio/sequencer/MonoStepSequencer.hpp"
#include "magda/daw/audio/sequencer/PolyStepSequencer.hpp"

// The sequencing core (#2313): patterns and blocks in, notes out, with no
// device, no ValueTree and no engine anywhere in the picture. These are the
// rules the mono and poly step sequencers play by - what a rest, a tie, a
// glide, a gate length and a probability roll each do to the notes that come
// out - held here rather than inside a device, so the live device, the offline
// clip export and an agent all answer to the same tests.

namespace seq = magda::daw::audio::sequencer;
using Approx = Catch::Approx;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSamples = 512;
/// 120 bpm: two beats a second, so a 512-sample block spans 512/24000 beats.
constexpr double kBeatsPerBlock = kBlockSamples / 24000.0;
constexpr double kBlockSeconds = kBlockSamples / kSampleRate;

/// A note event with the time it landed on the timeline, not in its block.
struct Played {
    double time = 0.0;
    int noteNumber = 0;
    int velocity = 0;
    bool isNoteOn = false;
};

/// Collects what a sequencer plays across a run of blocks.
struct Recorder : seq::NoteSink {
    double blockStart = 0.0;
    std::vector<Played> played;

    void addNoteEvent(const seq::NoteEvent& event) override {
        played.push_back({.time = blockStart + event.timeInBlock,
                          .noteNumber = event.noteNumber,
                          .velocity = event.velocity,
                          .isNoteOn = event.isNoteOn});
    }

    std::vector<int> noteOnNumbers() const {
        std::vector<int> out;
        for (const auto& event : played)
            if (event.isNoteOn)
                out.push_back(event.noteNumber);
        return out;
    }

    int noteOnCount() const {
        return static_cast<int>(noteOnNumbers().size());
    }
};

/// Whole blocks that fit inside @p beats of timeline. Deliberately short of
/// the boundary: a run of "one beat" must not catch the step that lands on it.
int blocksFor(double beats) {
    return static_cast<int>(beats / kBeatsPerBlock);
}

/// Drives a sequencer over equal blocks from beat 0, at 120 bpm.
template <typename SequencerT, typename PatternT, typename ParamsT> struct Runner {
    SequencerT sequencer;
    Recorder recorder;
    double beat = 0.0;

    Runner() {
        sequencer.setSampleRate(kSampleRate);
    }

    void run(int blocks, const PatternT& pattern, const ParamsT& params, bool playing = true) {
        for (int i = 0; i < blocks; ++i) {
            const seq::StepClock::BlockTiming timing{.startBeat = beat,
                                                     .endBeat = beat + kBeatsPerBlock,
                                                     .isPlaying = playing,
                                                     .numSamples = kBlockSamples};
            sequencer.processBlock(timing, pattern, params, recorder);
            beat += kBeatsPerBlock;
            recorder.blockStart += kBlockSeconds;
        }
    }
};

using MonoRunner = Runner<seq::MonoStepSequencer, seq::MonoPattern, seq::MonoStepSequencer::Params>;
using PolyRunner = Runner<seq::PolyStepSequencer, seq::PolyPattern, seq::PolyStepSequencer::Params>;

/// A four-step mono pattern on consecutive notes, every step playing.
seq::MonoPattern fourNotePattern() {
    seq::MonoPattern pattern;
    pattern.length = 4;
    const int notes[] = {60, 62, 64, 65};
    for (int i = 0; i < 4; ++i) {
        auto& step = pattern.steps[static_cast<size_t>(i)];
        step.noteNumber = notes[i];
        step.gate = true;
    }
    return pattern;
}

seq::MonoStepSequencer::Params monoParams() {
    seq::MonoStepSequencer::Params params;
    params.rate = seq::StepClock::Rate::Sixteenth;
    params.gateLength = 0.5f;
    params.accentVelocity = 120;
    params.normalVelocity = 90;
    return params;
}

/// Every note-on is answered by a note-off for the same note, in order.
bool notesAreBalanced(const std::vector<Played>& played) {
    int sounding = 0;
    for (const auto& event : played) {
        sounding += event.isNoteOn ? 1 : -1;
        if (sounding < 0)
            return false;
    }
    return sounding == 0;
}

}  // namespace

// ============================================================================
// Mono sequencer
// ============================================================================

TEST_CASE("MonoStepSequencer walks its pattern in order", "[sequencer][mono]") {
    MonoRunner runner;
    // Two beats at a sixteenth per step is eight steps: two passes of four.
    runner.run(blocksFor(2.0), fourNotePattern(), monoParams());

    const std::vector<int> expected{60, 62, 64, 65, 60, 62, 64, 65};
    REQUIRE(runner.recorder.noteOnNumbers() == expected);
    REQUIRE(notesAreBalanced(runner.recorder.played));
}

TEST_CASE("MonoStepSequencer shifts a step by whole octaves", "[sequencer][mono]") {
    auto pattern = fourNotePattern();
    pattern.steps[1].octaveShift = 1;
    pattern.steps[2].octaveShift = -2;

    MonoRunner runner;
    runner.run(blocksFor(1.0), pattern, monoParams());

    const auto notes = runner.recorder.noteOnNumbers();
    REQUIRE(notes.size() >= 4);
    REQUIRE(notes[0] == 60);
    REQUIRE(notes[1] == 62 + 12);
    REQUIRE(notes[2] == 64 - 24);
    REQUIRE(notes[3] == 65);
}

TEST_CASE("MonoStepSequencer plays an accented step louder", "[sequencer][mono]") {
    auto pattern = fourNotePattern();
    pattern.steps[2].accent = true;

    MonoRunner runner;
    runner.run(blocksFor(1.0), pattern, monoParams());

    std::vector<int> velocities;
    for (const auto& event : runner.recorder.played)
        if (event.isNoteOn)
            velocities.push_back(event.velocity);

    REQUIRE(velocities.size() >= 4);
    REQUIRE(velocities[0] == 90);
    REQUIRE(velocities[1] == 90);
    REQUIRE(velocities[2] == 120);
    REQUIRE(velocities[3] == 90);
}

TEST_CASE("MonoStepSequencer skips a rest and releases what was sounding", "[sequencer][mono]") {
    auto pattern = fourNotePattern();
    pattern.steps[1].gate = false;

    MonoRunner runner;
    runner.run(blocksFor(1.0), pattern, monoParams());

    const std::vector<int> expected{60, 64, 65};
    REQUIRE(runner.recorder.noteOnNumbers() == expected);
    REQUIRE(notesAreBalanced(runner.recorder.played));
}

TEST_CASE("MonoStepSequencer holds a note through a tie instead of retriggering",
          "[sequencer][mono]") {
    auto pattern = fourNotePattern();
    pattern.steps[1].tie = true;

    MonoRunner runner;
    runner.run(blocksFor(1.0), pattern, monoParams());

    // Step 1 adds no note of its own: the pass plays three notes, not four.
    const std::vector<int> expected{60, 64, 65};
    REQUIRE(runner.recorder.noteOnNumbers() == expected);

    // And the tied note is still sounding when step 2 arrives - it is released
    // by step 2's note-off, a whole step later than a gate of 0.5 would give.
    const auto& played = runner.recorder.played;
    REQUIRE(played.size() >= 3);
    REQUIRE(played[0].isNoteOn);
    REQUIRE(played[0].noteNumber == 60);
    REQUIRE_FALSE(played[1].isNoteOn);
    REQUIRE(played[1].noteNumber == 60);
    // One sixteenth at 120 bpm is 0.125 s; the tie carries the note across two.
    REQUIRE(played[1].time - played[0].time == Approx(0.25).margin(0.002));
}

TEST_CASE("MonoStepSequencer releases a note after its gate length", "[sequencer][mono]") {
    auto params = monoParams();
    params.gateLength = 0.5f;

    MonoRunner runner;
    runner.run(blocksFor(1.0), fourNotePattern(), params);

    const auto& played = runner.recorder.played;
    REQUIRE(played.size() >= 2);
    REQUIRE(played[0].isNoteOn);
    REQUIRE_FALSE(played[1].isNoteOn);
    // Half of a sixteenth at 120 bpm.
    REQUIRE(played[1].time - played[0].time == Approx(0.0625).margin(0.001));
}

TEST_CASE("MonoStepSequencer holds a gliding step for its whole length", "[sequencer][mono]") {
    auto pattern = fourNotePattern();
    pattern.steps[0].glide = true;

    auto params = monoParams();
    params.gateLength = 0.25f;

    MonoRunner runner;
    runner.run(blocksFor(1.0), pattern, params);

    const auto& played = runner.recorder.played;
    REQUIRE(played.size() >= 2);
    REQUIRE(played[0].noteNumber == 60);
    REQUIRE_FALSE(played[1].isNoteOn);
    // A glide ignores the gate length: the note reaches the next step, which
    // releases it just before its own note-on.
    REQUIRE(played[1].time - played[0].time == Approx(0.125).margin(0.002));
}

TEST_CASE("MonoStepSequencer is silent while the transport is stopped", "[sequencer][mono]") {
    MonoRunner runner;
    runner.run(blocksFor(2.0), fourNotePattern(), monoParams(), /*playing=*/false);

    REQUIRE(runner.recorder.played.empty());
    REQUIRE(runner.sequencer.currentStep() == -1);
    REQUIRE_FALSE(runner.sequencer.isRunning());
}

TEST_CASE("MonoStepSequencer releases its note when the transport stops", "[sequencer][mono]") {
    auto params = monoParams();
    params.gateLength = 1.0f;  // still sounding when the stop arrives

    MonoRunner runner;
    runner.run(blocksFor(0.2), fourNotePattern(), params);
    REQUIRE(runner.recorder.noteOnCount() >= 1);

    runner.run(1, fourNotePattern(), params, /*playing=*/false);
    REQUIRE(notesAreBalanced(runner.recorder.played));
    REQUIRE(runner.sequencer.soundingNote() == -1);
    REQUIRE(runner.sequencer.currentStep() == -1);
}

TEST_CASE("MonoStepSequencer publishes the step it is on", "[sequencer][mono]") {
    MonoRunner runner;
    auto pattern = fourNotePattern();

    // A sixteenth is 0.125 s; three of them in, the pattern is on step 2.
    runner.run(blocksFor(0.6), pattern, monoParams());
    REQUIRE(runner.sequencer.currentStep() == 2);
    REQUIRE(runner.sequencer.isRunning());
}

TEST_CASE("MonoStepSequencer wraps a pattern shorter than the grid", "[sequencer][mono]") {
    auto pattern = fourNotePattern();
    pattern.length = 2;

    MonoRunner runner;
    runner.run(blocksFor(1.0), pattern, monoParams());

    const std::vector<int> expected{60, 62, 60, 62};
    REQUIRE(runner.recorder.noteOnNumbers() == expected);
}

// ============================================================================
// Poly sequencer
// ============================================================================

namespace {

/// A two-step poly pattern: a C major triad, then a single note.
seq::PolyPattern chordPattern() {
    seq::PolyPattern pattern;
    pattern.length = 2;

    auto& chord = pattern.steps[0];
    chord.gate = true;
    chord.velocity = 100;
    chord.noteCount = 3;
    chord.notes[0] = {.noteNumber = 60};
    chord.notes[1] = {.noteNumber = 64};
    chord.notes[2] = {.noteNumber = 67};

    auto& single = pattern.steps[1];
    single.gate = true;
    single.velocity = 80;
    single.noteCount = 1;
    single.notes[0] = {.noteNumber = 72};

    return pattern;
}

seq::PolyStepSequencer::Params polyParams() {
    seq::PolyStepSequencer::Params params;
    params.rate = seq::StepClock::Rate::Sixteenth;
    params.gateLength = 0.5f;
    return params;
}

}  // namespace

TEST_CASE("PolyStepSequencer sounds a step's notes together", "[sequencer][poly]") {
    PolyRunner runner;
    // Short of the gate length, so the chord is still sounding at the end.
    runner.run(blocksFor(0.1), chordPattern(), polyParams());

    const auto& played = runner.recorder.played;
    REQUIRE(played.size() >= 3);
    REQUIRE(played[0].isNoteOn);
    REQUIRE(played[1].isNoteOn);
    REQUIRE(played[2].isNoteOn);
    REQUIRE(played[0].noteNumber == 60);
    REQUIRE(played[1].noteNumber == 64);
    REQUIRE(played[2].noteNumber == 67);
    REQUIRE(played[1].time == Approx(played[0].time));
    REQUIRE(played[2].time == Approx(played[0].time));
    REQUIRE(runner.sequencer.soundingNoteCount() == 3);
    REQUIRE(runner.sequencer.soundingNote() == 60);
}

TEST_CASE("PolyStepSequencer takes velocity from the step, or the note that overrides it",
          "[sequencer][poly]") {
    auto pattern = chordPattern();
    pattern.steps[0].notes[1].velocity = 40;

    PolyRunner runner;
    runner.run(blocksFor(0.2), pattern, polyParams());

    const auto& played = runner.recorder.played;
    REQUIRE(played.size() >= 3);
    REQUIRE(played[0].velocity == 100);
    REQUIRE(played[1].velocity == 40);
    REQUIRE(played[2].velocity == 100);
}

TEST_CASE("PolyStepSequencer releases the whole chord together", "[sequencer][poly]") {
    PolyRunner runner;
    runner.run(blocksFor(1.0), chordPattern(), polyParams());

    REQUIRE(notesAreBalanced(runner.recorder.played));

    // The three note-offs that end the first chord share a time.
    const auto& played = runner.recorder.played;
    std::vector<double> offTimes;
    for (const auto& event : played)
        if (!event.isNoteOn && offTimes.size() < 3)
            offTimes.push_back(event.time);
    REQUIRE(offTimes.size() == 3);
    REQUIRE(offTimes[1] == Approx(offTimes[0]));
    REQUIRE(offTimes[2] == Approx(offTimes[0]));
}

TEST_CASE("PolyStepSequencer holds a chord through a tie", "[sequencer][poly]") {
    auto pattern = chordPattern();
    pattern.steps[1].tie = true;

    PolyRunner runner;
    runner.run(blocksFor(0.5), pattern, polyParams());

    // Only the first step's chord ever sounds: the tie adds no note-ons of its
    // own, and step 1's own note never plays.
    for (const auto& event : runner.recorder.played)
        REQUIRE(event.noteNumber != 72);

    // The chord is still sounding when the run ends, two sixteenths in: a tie
    // holds it past its gate, and past the stuck-note guard that used to cut
    // any tie longer than a few blocks.
    REQUIRE(runner.sequencer.soundingNoteCount() == 3);
    REQUIRE(runner.recorder.noteOnCount() == 3);
}

TEST_CASE("PolyStepSequencer plays a rest and an empty step as silence", "[sequencer][poly]") {
    auto pattern = chordPattern();
    pattern.steps[0].gate = false;
    pattern.steps[1].noteCount = 0;

    PolyRunner runner;
    runner.run(blocksFor(1.0), pattern, polyParams());

    REQUIRE(runner.recorder.played.empty());
}

TEST_CASE("PolyStepSequencer skips a step whose probability roll fails", "[sequencer][poly]") {
    auto pattern = chordPattern();
    pattern.steps[0].probability = 0.0f;

    PolyRunner never;
    never.run(blocksFor(1.0), pattern, polyParams());
    for (const auto& event : never.recorder.played)
        REQUIRE(event.noteNumber == 72);

    // A certain step is never rolled away, whatever the seed.
    auto certain = chordPattern();
    certain.steps[0].probability = 1.0f;
    PolyRunner always;
    always.sequencer.setRandomSeed(12345);
    always.run(blocksFor(1.0), certain, polyParams());
    REQUIRE(always.recorder.noteOnCount() >= 4);
}

TEST_CASE("PolyStepSequencer releases its chord when the transport stops", "[sequencer][poly]") {
    auto params = polyParams();
    params.gateLength = 1.0f;

    PolyRunner runner;
    runner.run(blocksFor(0.1), chordPattern(), params);
    REQUIRE(runner.recorder.noteOnCount() >= 3);

    runner.run(1, chordPattern(), params, /*playing=*/false);
    REQUIRE(notesAreBalanced(runner.recorder.played));
    REQUIRE(runner.sequencer.soundingNote() == -1);
    REQUIRE(runner.sequencer.currentStep() == -1);
}
