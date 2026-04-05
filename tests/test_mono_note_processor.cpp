#include <array>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/audio/MonoNoteProcessor.hpp"

using MonoNoteProcessor = magda::daw::audio::MonoNoteProcessor;
using Step = MonoNoteProcessor::Step;
using StepEvent = MonoNoteProcessor::StepEvent;
using MidiOutput = MonoNoteProcessor::MidiOutput;
using BlockParams = MonoNoteProcessor::BlockParams;

// ============================================================================
// Helpers
// ============================================================================

namespace {

/** Default params: 120 BPM, 1/16 rate, 50% gate, 512 buffer, 44100 sr. */
BlockParams defaultParams() {
    BlockParams p;
    p.gateLength = 0.5f;
    p.accentVelocity = 110;
    p.normalVelocity = 80;
    // 1/16 at 120 BPM = 0.25 beats = 0.125 sec = 5512 samples
    p.stepDurationSamples = 5512;
    p.bufferSamples = 512;
    p.sampleRate = 44100.0;
    return p;
}

/** Simple 4-step pattern: C4, D4, E4, F4, all gated. */
std::array<Step, 4> basicPattern() {
    std::array<Step, 4> steps{};
    steps[0].noteNumber = 60;
    steps[1].noteNumber = 62;
    steps[2].noteNumber = 64;
    steps[3].noteNumber = 65;
    return steps;
}

/** Count note-on events in output. */
int countNoteOns(const std::vector<MidiOutput>& out) {
    int n = 0;
    for (auto& e : out)
        if (e.type == MidiOutput::NoteOn)
            ++n;
    return n;
}

/** Count note-off events in output. */
int countNoteOffs(const std::vector<MidiOutput>& out) {
    int n = 0;
    for (auto& e : out)
        if (e.type == MidiOutput::NoteOff)
            ++n;
    return n;
}

/** Check that every note-on has a corresponding note-off (eventually). */
bool allNotesResolved(const std::vector<MidiOutput>& out) {
    int open = 0;
    for (auto& e : out) {
        if (e.type == MidiOutput::NoteOn)
            ++open;
        else if (e.type == MidiOutput::NoteOff)
            --open;
    }
    return open <= 0;
}

/** Run N empty blocks (no step events) through the processor. */
void runEmptyBlocks(MonoNoteProcessor& proc, const Step* steps, int stepCount,
                    const BlockParams& params, int numBlocks, std::vector<MidiOutput>& output) {
    for (int i = 0; i < numBlocks; ++i)
        proc.processBlock(nullptr, 0, steps, stepCount, params, output);
}

}  // namespace

// ============================================================================
// Basic note-on / note-off
// ============================================================================

TEST_CASE("Single step fires note-on", "[mono][basic]") {
    MonoNoteProcessor proc;
    auto steps = basicPattern();
    auto params = defaultParams();
    std::vector<MidiOutput> out;

    StepEvent evt{0, 0.001};
    proc.processBlock(&evt, 1, steps.data(), 4, params, out);

    REQUIRE(countNoteOns(out) == 1);
    REQUIRE(out[0].type == MidiOutput::NoteOn);
    REQUIRE(out[0].noteNumber == 60);
    REQUIRE(out[0].velocity == 80);
}

TEST_CASE("Note-off fires after countdown expires", "[mono][gate]") {
    MonoNoteProcessor proc;
    auto steps = basicPattern();
    auto params = defaultParams();
    std::vector<MidiOutput> out;

    // Fire step 0 at start of block
    StepEvent evt{0, 0.0};
    proc.processBlock(&evt, 1, steps.data(), 4, params, out);

    REQUIRE(proc.lastPlayedNote() == 60);
    REQUIRE(proc.noteOffCountdown() > 0);

    // Run blocks until countdown expires
    // 50% gate of 5512 = 2756 samples. ~5.4 blocks of 512.
    out.clear();
    runEmptyBlocks(proc, steps.data(), 4, params, 6, out);

    REQUIRE(countNoteOffs(out) == 1);
    REQUIRE(proc.lastPlayedNote() == -1);
}

TEST_CASE("Accent uses accent velocity", "[mono][accent]") {
    MonoNoteProcessor proc;
    auto steps = basicPattern();
    steps[0].accent = true;
    auto params = defaultParams();
    std::vector<MidiOutput> out;

    StepEvent evt{0, 0.0};
    proc.processBlock(&evt, 1, steps.data(), 4, params, out);

    REQUIRE(out[0].velocity == 110);
}

// ============================================================================
// Consecutive notes: mono guarantee
// ============================================================================

TEST_CASE("Consecutive step triggers note-off before note-on", "[mono][transition]") {
    MonoNoteProcessor proc;
    auto steps = basicPattern();
    auto params = defaultParams();
    std::vector<MidiOutput> out;

    // Step 0 fires
    StepEvent evt0{0, 0.001};
    proc.processBlock(&evt0, 1, steps.data(), 4, params, out);
    REQUIRE(proc.lastPlayedNote() == 60);

    // Step 1 fires in a later block
    out.clear();
    StepEvent evt1{1, 0.002};
    proc.processBlock(&evt1, 1, steps.data(), 4, params, out);

    // Should see: note-off(60), note-on(62)
    REQUIRE(out.size() >= 2);

    // Find the note-off for 60 and note-on for 62
    bool foundOff60 = false;
    bool foundOn62 = false;
    double offTime = -1, onTime = -1;
    for (auto& e : out) {
        if (e.type == MidiOutput::NoteOff && e.noteNumber == 60) {
            foundOff60 = true;
            offTime = e.time;
        }
        if (e.type == MidiOutput::NoteOn && e.noteNumber == 62) {
            foundOn62 = true;
            onTime = e.time;
        }
    }
    REQUIRE(foundOff60);
    REQUIRE(foundOn62);
    REQUIRE(offTime < onTime);  // note-off strictly before note-on
}

TEST_CASE("Two steps in same block: both play, mono maintained", "[mono][transition]") {
    MonoNoteProcessor proc;
    auto steps = basicPattern();
    auto params = defaultParams();
    std::vector<MidiOutput> out;

    StepEvent evts[2] = {{0, 0.001}, {1, 0.005}};
    proc.processBlock(evts, 2, steps.data(), 4, params, out);

    // Should have note-on(60), note-off(60), note-on(62) at minimum
    REQUIRE(countNoteOns(out) == 2);
    REQUIRE(proc.lastPlayedNote() == 62);

    // Verify ordering: every note-off precedes its next note-on
    int open = 0;
    for (auto& e : out) {
        if (e.type == MidiOutput::NoteOn)
            ++open;
        else
            --open;
        REQUIRE(open <= 1);  // Never more than 1 note open
    }
}

// ============================================================================
// Rest steps (gate = false)
// ============================================================================

TEST_CASE("Rest step kills playing note", "[mono][rest]") {
    MonoNoteProcessor proc;
    auto steps = basicPattern();
    steps[1].gate = false;
    auto params = defaultParams();
    std::vector<MidiOutput> out;

    // Play step 0
    StepEvent evt0{0, 0.001};
    proc.processBlock(&evt0, 1, steps.data(), 4, params, out);
    REQUIRE(proc.lastPlayedNote() == 60);

    // Step 1 is a rest
    out.clear();
    StepEvent evt1{1, 0.001};
    proc.processBlock(&evt1, 1, steps.data(), 4, params, out);

    REQUIRE(countNoteOffs(out) == 1);
    REQUIRE(proc.lastPlayedNote() == -1);
}

TEST_CASE("Rest step with no playing note emits nothing", "[mono][rest]") {
    MonoNoteProcessor proc;
    auto steps = basicPattern();
    steps[0].gate = false;
    auto params = defaultParams();
    std::vector<MidiOutput> out;

    StepEvent evt{0, 0.001};
    proc.processBlock(&evt, 1, steps.data(), 4, params, out);

    REQUIRE(out.empty());
    REQUIRE(proc.lastPlayedNote() == -1);
}

// ============================================================================
// Tie steps
// ============================================================================

TEST_CASE("Tie step holds previous note without retrigger", "[mono][tie]") {
    MonoNoteProcessor proc;
    auto steps = basicPattern();
    steps[1].tie = true;
    auto params = defaultParams();
    std::vector<MidiOutput> out;

    // Play step 0
    StepEvent evt0{0, 0.001};
    proc.processBlock(&evt0, 1, steps.data(), 4, params, out);
    REQUIRE(proc.lastPlayedNote() == 60);

    // Step 1 is a tie
    out.clear();
    StepEvent evt1{1, 0.001};
    proc.processBlock(&evt1, 1, steps.data(), 4, params, out);

    // No note-on or note-off: note holds
    REQUIRE(countNoteOns(out) == 0);
    REQUIRE(countNoteOffs(out) == 0);
    REQUIRE(proc.lastPlayedNote() == 60);
    REQUIRE(proc.noteOffCountdown() == 0);  // Countdown cancelled
}

TEST_CASE("Tie step with no playing note acts as normal trigger", "[mono][tie]") {
    MonoNoteProcessor proc;
    auto steps = basicPattern();
    steps[0].tie = true;
    auto params = defaultParams();
    std::vector<MidiOutput> out;

    // Step 0 is tie but no note playing — should play normally
    StepEvent evt{0, 0.001};
    proc.processBlock(&evt, 1, steps.data(), 4, params, out);

    REQUIRE(countNoteOns(out) == 1);
    REQUIRE(proc.lastPlayedNote() == 60);
}

TEST_CASE("Tie chain: multiple ties hold the note", "[mono][tie]") {
    MonoNoteProcessor proc;
    Step steps[4]{};
    steps[0] = {60, 0, true, false, false, false};
    steps[1] = {62, 0, true, false, false, true};   // tie
    steps[2] = {64, 0, true, false, false, true};   // tie
    steps[3] = {65, 0, true, false, false, false};  // normal
    auto params = defaultParams();
    std::vector<MidiOutput> out;

    // Step 0: normal note-on
    StepEvent evt0{0, 0.001};
    proc.processBlock(&evt0, 1, steps, 4, params, out);
    REQUIRE(proc.lastPlayedNote() == 60);

    // Step 1: tie — holds
    out.clear();
    StepEvent evt1{1, 0.001};
    proc.processBlock(&evt1, 1, steps, 4, params, out);
    REQUIRE(proc.lastPlayedNote() == 60);
    REQUIRE(countNoteOns(out) == 0);

    // Step 2: tie — still holds
    out.clear();
    StepEvent evt2{2, 0.001};
    proc.processBlock(&evt2, 1, steps, 4, params, out);
    REQUIRE(proc.lastPlayedNote() == 60);
    REQUIRE(countNoteOns(out) == 0);

    // Step 3: normal — kills old, plays new
    out.clear();
    StepEvent evt3{3, 0.001};
    proc.processBlock(&evt3, 1, steps, 4, params, out);
    REQUIRE(proc.lastPlayedNote() == 65);
    REQUIRE(countNoteOns(out) == 1);
    REQUIRE(countNoteOffs(out) == 1);
}

// ============================================================================
// Glide (100% gate)
// ============================================================================

TEST_CASE("Glide step uses 100% gate", "[mono][glide]") {
    MonoNoteProcessor proc;
    auto steps = basicPattern();
    steps[0].glide = true;
    auto params = defaultParams();
    params.gateLength = 0.5f;  // Would normally be 50% but glide overrides
    std::vector<MidiOutput> out;

    StepEvent evt{0, 0.0};
    proc.processBlock(&evt, 1, steps.data(), 4, params, out);

    // With 100% gate: countdown = stepDuration - bufferSamples = 5512 - 512 = 5000
    REQUIRE(proc.noteOffCountdown() == 5000);
}

TEST_CASE("Non-glide step uses gate length parameter", "[mono][glide]") {
    MonoNoteProcessor proc;
    auto steps = basicPattern();
    auto params = defaultParams();
    params.gateLength = 0.5f;
    std::vector<MidiOutput> out;

    StepEvent evt{0, 0.0};
    proc.processBlock(&evt, 1, steps.data(), 4, params, out);

    // 50% gate: gateSamples = 5512 * 0.5 = 2756
    // countdown = 2756 - 512 = 2244
    REQUIRE(proc.noteOffCountdown() == 2244);
}

TEST_CASE("Next step is tie forces 100% gate on current step", "[mono][glide][tie]") {
    MonoNoteProcessor proc;
    auto steps = basicPattern();
    steps[1].tie = true;  // Next step is tie
    auto params = defaultParams();
    params.gateLength = 0.3f;  // Would be 30% but overridden
    std::vector<MidiOutput> out;

    StepEvent evt{0, 0.0};
    proc.processBlock(&evt, 1, steps.data(), 4, params, out);

    // 100% gate because next is tie: countdown = 5512 - 512 = 5000
    REQUIRE(proc.noteOffCountdown() == 5000);
}

// ============================================================================
// Countdown expiration with step interaction
// ============================================================================

TEST_CASE("Countdown fires before step event: note-off emitted", "[mono][countdown]") {
    MonoNoteProcessor proc;
    auto steps = basicPattern();
    auto params = defaultParams();
    params.gateLength = 0.1f;  // Very short gate
    std::vector<MidiOutput> out;

    // Play step 0 — short gate so countdown is small
    StepEvent evt0{0, 0.0};
    proc.processBlock(&evt0, 1, steps.data(), 4, params, out);

    // gateSamples = 5512 * 0.1 = 551
    // countdown = 551 - 512 = 39
    REQUIRE(proc.noteOffCountdown() == 39);
    REQUIRE(proc.lastPlayedNote() == 60);

    // Next block: countdown (39 samples = 0.000884s) fires before step at 0.005s
    out.clear();
    StepEvent evt1{1, 0.005};
    proc.processBlock(&evt1, 1, steps.data(), 4, params, out);

    // Should see: note-off(60) from countdown, then note-on(62)
    REQUIRE(countNoteOffs(out) >= 1);
    REQUIRE(countNoteOns(out) == 1);
    REQUIRE(proc.lastPlayedNote() == 62);
}

TEST_CASE("Step fires before countdown: step handles transition", "[mono][countdown]") {
    MonoNoteProcessor proc;
    auto steps = basicPattern();
    auto params = defaultParams();
    std::vector<MidiOutput> out;

    // Play step 0 with moderate gate
    StepEvent evt0{0, 0.0};
    proc.processBlock(&evt0, 1, steps.data(), 4, params, out);
    REQUIRE(proc.lastPlayedNote() == 60);

    // Manually set a high countdown so it won't fire before the step
    // (In real code this happens when countdown > bufferSamples)
    out.clear();

    // Next block with step 1 at start — step fires first
    StepEvent evt1{1, 0.0001};
    proc.processBlock(&evt1, 1, steps.data(), 4, params, out);

    // Step handles the transition: note-off(60), note-on(62)
    REQUIRE(proc.lastPlayedNote() == 62);
}

TEST_CASE("Countdown fires but next step is tie: note holds", "[mono][countdown][tie]") {
    MonoNoteProcessor proc;
    auto steps = basicPattern();
    steps[1].tie = true;
    steps[1].gate = true;
    auto params = defaultParams();
    params.gateLength = 0.1f;
    std::vector<MidiOutput> out;

    // Play step 0
    StepEvent evt0{0, 0.0};
    proc.processBlock(&evt0, 1, steps.data(), 4, params, out);
    REQUIRE(proc.noteOffCountdown() > 0);

    // Step 1 (tie) fires while countdown is pending
    out.clear();
    StepEvent evt1{1, 0.005};
    proc.processBlock(&evt1, 1, steps.data(), 4, params, out);

    // Tie should hold the note — no note-off
    REQUIRE(proc.lastPlayedNote() == 60);
    REQUIRE(countNoteOffs(out) == 0);
}

// ============================================================================
// Note-off within same block (very short gate or late note-on)
// ============================================================================

TEST_CASE("Very short gate: note-off in same block as note-on", "[mono][gate][short]") {
    MonoNoteProcessor proc;
    auto steps = basicPattern();
    auto params = defaultParams();
    params.gateLength = 0.05f;  // Minimum gate
    // gateSamples = 5512 * 0.05 = 275
    // noteOnSample at time 0.0 = 0
    // countdown = 275 - (512 - 0) = -237 → note-off in same block
    std::vector<MidiOutput> out;

    StepEvent evt{0, 0.0};
    proc.processBlock(&evt, 1, steps.data(), 4, params, out);

    // Note fires and dies in same block
    REQUIRE(countNoteOns(out) == 1);
    REQUIRE(countNoteOffs(out) == 1);
    REQUIRE(proc.lastPlayedNote() == -1);
    REQUIRE(proc.noteOffCountdown() == 0);
}

TEST_CASE("Late note-on in block can trigger same-block note-off", "[mono][gate][short]") {
    MonoNoteProcessor proc;
    auto steps = basicPattern();
    auto params = defaultParams();
    params.gateLength = 0.05f;
    // noteOnSample at time 0.01s = 441 samples
    // countdown = 275 - (512 - 441) = 275 - 71 = 204 → positive, spans next block
    std::vector<MidiOutput> out;

    StepEvent evt{0, 0.01};
    proc.processBlock(&evt, 1, steps.data(), 4, params, out);

    // Late enough that countdown is positive → note-off next block
    REQUIRE(countNoteOns(out) == 1);
    REQUIRE(proc.lastPlayedNote() == 60);
    REQUIRE(proc.noteOffCountdown() > 0);
}

// ============================================================================
// Octave shift
// ============================================================================

TEST_CASE("Octave shift resolves correctly", "[mono][octave]") {
    MonoNoteProcessor proc;
    Step steps[2]{};
    steps[0] = {60, 2, true, false, false, false};   // C4 + 2 octaves = C6 (84)
    steps[1] = {60, -1, true, false, false, false};  // C4 - 1 octave = C3 (48)
    auto params = defaultParams();
    std::vector<MidiOutput> out;

    StepEvent evt0{0, 0.001};
    proc.processBlock(&evt0, 1, steps, 2, params, out);
    REQUIRE(out[0].noteNumber == 84);

    out.clear();
    StepEvent evt1{1, 0.001};
    proc.processBlock(&evt1, 1, steps, 2, params, out);
    // Find the note-on
    for (auto& e : out) {
        if (e.type == MidiOutput::NoteOn) {
            REQUIRE(e.noteNumber == 48);
            break;
        }
    }
}

// ============================================================================
// Stuck note safety (silent block counter)
// ============================================================================

TEST_CASE("Stuck note killed after 5 silent blocks", "[mono][safety]") {
    MonoNoteProcessor proc;
    auto steps = basicPattern();
    auto params = defaultParams();
    std::vector<MidiOutput> out;

    // Play a note
    StepEvent evt{0, 0.001};
    proc.processBlock(&evt, 1, steps.data(), 4, params, out);
    REQUIRE(proc.lastPlayedNote() == 60);

    // Force state where countdown is 0 but note is playing (stuck)
    // This happens naturally if countdown was cleared by a tie then the tie chain ends
    // without a new note. Simulate by running enough blocks for countdown to expire.
    out.clear();
    runEmptyBlocks(proc, steps.data(), 4, params, 6, out);
    // Countdown should have expired by now
    out.clear();

    // Now note is stuck (lastPlayedNote=60, countdown=0)
    // If countdown already expired and killed the note, set it up manually
    if (proc.lastPlayedNote() < 0) {
        // Re-trigger to get stuck state
        StepEvent evt2{0, 0.001};
        proc.processBlock(&evt2, 1, steps.data(), 4, params, out);
        out.clear();
        // Simulate tie clearing countdown without killing note
        // by processing a tie step
        steps[1].tie = true;
        StepEvent tieEvt{1, 0.001};
        proc.processBlock(&tieEvt, 1, steps.data(), 4, params, out);
        out.clear();
        REQUIRE(proc.lastPlayedNote() == 60);
        REQUIRE(proc.noteOffCountdown() == 0);
    }

    // Now run 5 empty blocks — safety should kill the note
    out.clear();
    runEmptyBlocks(proc, steps.data(), 4, params, 5, out);
    REQUIRE(countNoteOffs(out) == 1);
    REQUIRE(proc.lastPlayedNote() == -1);
}

TEST_CASE("Silent block counter resets when step events fire", "[mono][safety]") {
    MonoNoteProcessor proc;
    auto steps = basicPattern();
    steps[1].tie = true;
    auto params = defaultParams();
    std::vector<MidiOutput> out;

    // Play step 0
    StepEvent evt0{0, 0.001};
    proc.processBlock(&evt0, 1, steps.data(), 4, params, out);

    // Tie step clears countdown
    out.clear();
    StepEvent evt1{1, 0.001};
    proc.processBlock(&evt1, 1, steps.data(), 4, params, out);
    REQUIRE(proc.noteOffCountdown() == 0);
    REQUIRE(proc.silentBlockCount() == 0);

    // 3 empty blocks
    out.clear();
    runEmptyBlocks(proc, steps.data(), 4, params, 3, out);
    REQUIRE(proc.silentBlockCount() == 3);

    // Step event resets counter
    out.clear();
    StepEvent evt2{2, 0.001};
    proc.processBlock(&evt2, 1, steps.data(), 4, params, out);
    REQUIRE(proc.silentBlockCount() == 0);
}

// ============================================================================
// Edge case: tie then rest — note must die
// ============================================================================

TEST_CASE("Tie followed by rest kills note", "[mono][tie][rest]") {
    MonoNoteProcessor proc;
    Step steps[4]{};
    steps[0] = {60, 0, true, false, false, false};
    steps[1] = {62, 0, true, false, false, true};    // tie
    steps[2] = {64, 0, false, false, false, false};  // rest
    steps[3] = {65, 0, true, false, false, false};
    auto params = defaultParams();
    std::vector<MidiOutput> out;

    StepEvent evt0{0, 0.001};
    proc.processBlock(&evt0, 1, steps, 4, params, out);
    REQUIRE(proc.lastPlayedNote() == 60);

    out.clear();
    StepEvent evt1{1, 0.001};
    proc.processBlock(&evt1, 1, steps, 4, params, out);
    REQUIRE(proc.lastPlayedNote() == 60);  // Tie holds

    out.clear();
    StepEvent evt2{2, 0.001};
    proc.processBlock(&evt2, 1, steps, 4, params, out);
    REQUIRE(proc.lastPlayedNote() == -1);  // Rest kills
    REQUIRE(countNoteOffs(out) == 1);
}

// ============================================================================
// Edge case: glide to rest — note should die at rest
// ============================================================================

TEST_CASE("Glide into rest step kills note at rest", "[mono][glide][rest]") {
    MonoNoteProcessor proc;
    Step steps[4]{};
    steps[0] = {60, 0, true, false, true, false};    // glide
    steps[1] = {62, 0, false, false, false, false};  // rest
    steps[2] = {64, 0, true, false, false, false};
    steps[3] = {65, 0, true, false, false, false};
    auto params = defaultParams();
    std::vector<MidiOutput> out;

    StepEvent evt0{0, 0.001};
    proc.processBlock(&evt0, 1, steps, 4, params, out);
    REQUIRE(proc.lastPlayedNote() == 60);

    // Rest step arrives
    out.clear();
    StepEvent evt1{1, 0.001};
    proc.processBlock(&evt1, 1, steps, 4, params, out);
    REQUIRE(proc.lastPlayedNote() == -1);
    REQUIRE(countNoteOffs(out) == 1);
}

// ============================================================================
// Edge case: wrap-around (last step → first step)
// ============================================================================

TEST_CASE("Pattern wrap-around: last step checks first for tie", "[mono][wrap]") {
    MonoNoteProcessor proc;
    Step steps[2]{};
    steps[0] = {60, 0, true, false, false, true};   // step 0: tie
    steps[1] = {62, 0, true, false, false, false};  // step 1: normal
    auto params = defaultParams();
    std::vector<MidiOutput> out;

    // Step 1 plays — next is step 0 which is tie → 100% gate
    StepEvent evt{1, 0.0};
    proc.processBlock(&evt, 1, steps, 2, params, out);
    REQUIRE(proc.noteOffCountdown() == params.stepDurationSamples - params.bufferSamples);
}

// ============================================================================
// Reset clears all state
// ============================================================================

TEST_CASE("Reset clears playing note and countdown", "[mono][reset]") {
    MonoNoteProcessor proc;
    auto steps = basicPattern();
    auto params = defaultParams();
    std::vector<MidiOutput> out;

    StepEvent evt{0, 0.001};
    proc.processBlock(&evt, 1, steps.data(), 4, params, out);
    REQUIRE(proc.lastPlayedNote() == 60);
    REQUIRE(proc.noteOffCountdown() > 0);

    proc.reset();
    REQUIRE(proc.lastPlayedNote() == -1);
    REQUIRE(proc.noteOffCountdown() == 0);
    REQUIRE(proc.silentBlockCount() == 0);
}

// ============================================================================
// Full pattern cycle: every note-on gets a note-off
// ============================================================================

TEST_CASE("Full pattern cycle: all notes resolve", "[mono][full]") {
    MonoNoteProcessor proc;
    auto steps = basicPattern();
    auto params = defaultParams();
    std::vector<MidiOutput> allOutput;

    // Simulate 4 steps, each in its own block, with empty blocks between
    for (int s = 0; s < 4; ++s) {
        StepEvent evt{s, 0.001};
        proc.processBlock(&evt, 1, steps.data(), 4, params, allOutput);

        // Run enough empty blocks for note-off
        runEmptyBlocks(proc, steps.data(), 4, params, 10, allOutput);
    }

    // Run extra blocks to flush any remaining notes
    runEmptyBlocks(proc, steps.data(), 4, params, 10, allOutput);

    REQUIRE(countNoteOns(allOutput) == 4);
    REQUIRE(allNotesResolved(allOutput));
    REQUIRE(proc.lastPlayedNote() == -1);
}

TEST_CASE("Full pattern with glides: all notes resolve", "[mono][full][glide]") {
    MonoNoteProcessor proc;
    Step steps[4]{};
    steps[0] = {60, 0, true, false, true, false};   // glide
    steps[1] = {62, 0, true, false, true, false};   // glide
    steps[2] = {64, 0, true, false, true, false};   // glide
    steps[3] = {65, 0, true, false, false, false};  // normal end
    auto params = defaultParams();
    std::vector<MidiOutput> allOutput;

    for (int s = 0; s < 4; ++s) {
        StepEvent evt{s, 0.001};
        proc.processBlock(&evt, 1, steps, 4, params, allOutput);
        runEmptyBlocks(proc, steps, 4, params, 12, allOutput);
    }
    runEmptyBlocks(proc, steps, 4, params, 10, allOutput);

    REQUIRE(countNoteOns(allOutput) == 4);
    REQUIRE(allNotesResolved(allOutput));
}

TEST_CASE("Full pattern with ties: all notes resolve", "[mono][full][tie]") {
    MonoNoteProcessor proc;
    Step steps[4]{};
    steps[0] = {60, 0, true, false, false, false};  // normal
    steps[1] = {62, 0, true, false, false, true};   // tie
    steps[2] = {64, 0, true, false, false, true};   // tie
    steps[3] = {65, 0, true, false, false, false};  // normal
    auto params = defaultParams();
    // Use 100% gate on step 0 since next is tie — this is what the real code does.
    // Note: nextIsTie in the processor forces 100% gate, so countdown will be large.
    // But after the tie sets countdown=0, we need to ensure steps arrive
    // within the safety window (5 blocks) so the note doesn't get killed.
    std::vector<MidiOutput> allOutput;

    // Step 0: normal, plays note 60
    StepEvent evt0{0, 0.001};
    proc.processBlock(&evt0, 1, steps, 4, params, allOutput);
    REQUIRE(proc.lastPlayedNote() == 60);

    // Run 2 empty blocks (within safety window since countdown is still active)
    runEmptyBlocks(proc, steps, 4, params, 2, allOutput);

    // Step 1: tie — holds note 60
    StepEvent evt1{1, 0.001};
    proc.processBlock(&evt1, 1, steps, 4, params, allOutput);
    REQUIRE(proc.lastPlayedNote() == 60);

    // Run 2 empty blocks (countdown=0, but within 5-block safety grace)
    runEmptyBlocks(proc, steps, 4, params, 2, allOutput);

    // Step 2: tie — holds note 60
    StepEvent evt2{2, 0.001};
    proc.processBlock(&evt2, 1, steps, 4, params, allOutput);
    REQUIRE(proc.lastPlayedNote() == 60);

    // Run 2 empty blocks
    runEmptyBlocks(proc, steps, 4, params, 2, allOutput);

    // Step 3: normal — kills 60, plays 65
    StepEvent evt3{3, 0.001};
    proc.processBlock(&evt3, 1, steps, 4, params, allOutput);
    REQUIRE(proc.lastPlayedNote() == 65);

    // Flush
    runEmptyBlocks(proc, steps, 4, params, 20, allOutput);

    // Only 2 note-ons: step 0 (normal) and step 3 (normal after tie chain)
    REQUIRE(countNoteOns(allOutput) == 2);
    REQUIRE(allNotesResolved(allOutput));
}

// ============================================================================
// Edge case: countdown across many blocks (large step duration)
// ============================================================================

TEST_CASE("Countdown decrements correctly across multiple blocks", "[mono][countdown]") {
    MonoNoteProcessor proc;
    auto steps = basicPattern();
    auto params = defaultParams();
    params.gateLength = 0.8f;
    // gateSamples = 5512 * 0.8 = 4409
    // countdown after note at t=0: 4409 - 512 = 3897
    std::vector<MidiOutput> out;

    StepEvent evt{0, 0.0};
    proc.processBlock(&evt, 1, steps.data(), 4, params, out);
    REQUIRE(proc.noteOffCountdown() == 3897);

    // Each empty block decrements by 512
    out.clear();
    runEmptyBlocks(proc, steps.data(), 4, params, 1, out);
    REQUIRE(proc.noteOffCountdown() == 3897 - 512);

    out.clear();
    runEmptyBlocks(proc, steps.data(), 4, params, 1, out);
    REQUIRE(proc.noteOffCountdown() == 3897 - 1024);
}

// ============================================================================
// Bug scenario: glide chain where note gets stuck
// ============================================================================

TEST_CASE("Glide chain with varying patterns does not leave stuck notes", "[mono][glide][stress]") {
    MonoNoteProcessor proc;
    Step steps[8]{};
    steps[0] = {60, 0, true, false, true, false};    // glide
    steps[1] = {62, 0, true, false, true, false};    // glide
    steps[2] = {64, 0, true, false, false, false};   // normal
    steps[3] = {65, 0, false, false, false, false};  // rest
    steps[4] = {67, 0, true, true, true, false};     // accent + glide
    steps[5] = {69, 0, true, false, false, true};    // tie
    steps[6] = {71, 0, true, false, false, false};   // normal
    steps[7] = {72, 0, true, false, true, false};    // glide

    auto params = defaultParams();
    std::vector<MidiOutput> allOutput;

    // Run through the pattern twice
    for (int cycle = 0; cycle < 2; ++cycle) {
        for (int s = 0; s < 8; ++s) {
            StepEvent evt{s, 0.001};
            proc.processBlock(&evt, 1, steps, 8, params, allOutput);
            runEmptyBlocks(proc, steps, 8, params, 12, allOutput);
        }
    }
    // Flush
    runEmptyBlocks(proc, steps, 8, params, 20, allOutput);

    REQUIRE(allNotesResolved(allOutput));
    REQUIRE(proc.lastPlayedNote() == -1);
}

// ============================================================================
// Stress: tight step timing (steps arrive every ~2 blocks)
// ============================================================================

TEST_CASE("Rapid glide steps with tight timing: mono maintained", "[mono][glide][stress]") {
    MonoNoteProcessor proc;
    Step steps[4]{};
    steps[0] = {60, 0, true, false, true, false};   // glide
    steps[1] = {64, 0, true, false, true, false};   // glide
    steps[2] = {67, 0, true, false, true, false};   // glide
    steps[3] = {72, 0, true, false, false, false};  // normal end

    auto params = defaultParams();
    // Fast rate: step every ~1000 samples (~2 blocks)
    params.stepDurationSamples = 1000;
    std::vector<MidiOutput> allOutput;

    // Run 10 cycles
    for (int cycle = 0; cycle < 10; ++cycle) {
        for (int s = 0; s < 4; ++s) {
            // Vary the timing within the block
            double t = (cycle % 3) * 0.002 + 0.001;
            StepEvent evt{s, t};
            proc.processBlock(&evt, 1, steps, 4, params, allOutput);
            // Only 1-2 empty blocks between steps (tight timing)
            runEmptyBlocks(proc, steps, 4, params, 1 + (s % 2), allOutput);
        }
    }
    runEmptyBlocks(proc, steps, 4, params, 20, allOutput);

    REQUIRE(allNotesResolved(allOutput));
    REQUIRE(proc.lastPlayedNote() == -1);

    // Check mono: never more than 1 note open at any point
    int open = 0;
    int maxOpen = 0;
    for (auto& e : allOutput) {
        if (e.type == MidiOutput::NoteOn)
            ++open;
        else
            --open;
        maxOpen = std::max(maxOpen, open);
    }
    REQUIRE(maxOpen <= 1);
}

TEST_CASE("Glide countdown exact boundary: step arrives when countdown expires",
          "[mono][glide][boundary]") {
    MonoNoteProcessor proc;
    Step steps[2]{};
    steps[0] = {60, 0, true, false, true, false};   // glide (100% gate)
    steps[1] = {64, 0, true, false, false, false};  // normal

    auto params = defaultParams();
    // Engineer exact boundary: step duration = 3 * buffer
    // So countdown after note at t=0: stepDuration - buffer = 2*buffer
    // After 2 empty blocks: countdown = 0 exactly
    // Then step 1 fires in the same block the countdown would expire
    params.stepDurationSamples = params.bufferSamples * 3;
    std::vector<MidiOutput> allOutput;

    StepEvent evt0{0, 0.0};
    proc.processBlock(&evt0, 1, steps, 2, params, allOutput);
    REQUIRE(proc.noteOffCountdown() == params.bufferSamples * 2);

    // 1 empty block: countdown = buffer
    runEmptyBlocks(proc, steps, 2, params, 1, allOutput);
    REQUIRE(proc.noteOffCountdown() == params.bufferSamples);

    // Step 1 fires at start of block, countdown fires same block
    allOutput.clear();
    StepEvent evt1{1, 0.0001};
    proc.processBlock(&evt1, 1, steps, 2, params, allOutput);

    // Should transition cleanly: note-off(60), note-on(64)
    REQUIRE(proc.lastPlayedNote() == 64);

    // Verify mono
    int open = 0;
    for (auto& e : allOutput) {
        if (e.type == MidiOutput::NoteOn)
            ++open;
        else
            --open;
        REQUIRE(open <= 1);
    }
}

// ============================================================================
// No events, no note playing: nothing happens
// ============================================================================

TEST_CASE("Empty blocks with no note: no output", "[mono][basic]") {
    MonoNoteProcessor proc;
    auto steps = basicPattern();
    auto params = defaultParams();
    std::vector<MidiOutput> out;

    runEmptyBlocks(proc, steps.data(), 4, params, 10, out);
    REQUIRE(out.empty());
    REQUIRE(proc.lastPlayedNote() == -1);
}
