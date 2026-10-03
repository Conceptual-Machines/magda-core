#include <juce_data_structures/juce_data_structures.h>

#include <catch2/catch_test_macros.hpp>

#include "DeviceTestState.hpp"
#include "magda/daw/audio/plugins/PolyStepSequencerPlugin.hpp"
#include "magda/daw/audio/plugins/StepSequencerPlugin.hpp"
#include "magda/daw/core/StepPatternState.hpp"

// The persisted shape of a step pattern (#2150). The element and property
// names below are the wire format: a project saved by any MAGDA version
// carries them, so they are asserted literally rather than through the
// device's own constants.

namespace audio = magda::daw::audio;

namespace {

juce::ValueTree monoStep(int index, int note, bool gate = true, bool accent = false) {
    juce::ValueTree step("STEP");
    step.setProperty("idx", index, nullptr);
    step.setProperty("note", note, nullptr);
    step.setProperty("gate", gate, nullptr);
    step.setProperty("accent", accent, nullptr);
    return step;
}

/// The state the model would hold for @p pattern: written by the host's own writer.
magda::sdk::StateNode hostState(const magda::sdk::sequencer::MonoPattern& pattern) {
    magda::device_state::Doc doc;
    magda::step_pattern::writeMono(doc, pattern);
    return audio::toSdkNode(doc.root);
}

magda::sdk::StateNode hostState(const magda::sdk::sequencer::PolyPattern& pattern) {
    magda::device_state::Doc doc;
    magda::step_pattern::writePoly(doc, pattern);
    return audio::toSdkNode(doc.root);
}

}  // namespace

TEST_CASE("A step sequencer restores only the STEP children", "[sequencer][state]") {
    juce::ValueTree state("STEPSEQ");
    state.setProperty("seqNumSteps", 8, nullptr);
    state.appendChild(monoStep(0, 48, true, true), nullptr);
    state.appendChild(juce::ValueTree("MODIFIERASSIGNMENTS"), nullptr);
    state.appendChild(monoStep(3, 55), nullptr);
    // Out of range: the model never writes one, a hand-edited project might.
    state.appendChild(monoStep(999, 60), nullptr);

    audio::StepSequencerPlugin sequencer;
    sequencer.restoreState(magda::test::stateOf(state));

    const auto pattern = sequencer.pattern();
    CHECK(pattern.length == 8);
    CHECK(pattern.step(0).noteNumber == 48);
    CHECK(pattern.step(0).accent);
    CHECK(pattern.step(3).noteNumber == 55);
    // Untouched steps keep their defaults rather than anything the foreign
    // children carried.
    CHECK(pattern.step(1) == audio::StepSequencerPlugin::Step{});
}

TEST_CASE("Restoring a step sequencer twice does not stack up steps", "[sequencer][state]") {
    juce::ValueTree state("STEPSEQ");
    state.appendChild(monoStep(0, 48), nullptr);
    state.appendChild(monoStep(1, 50), nullptr);

    audio::StepSequencerPlugin sequencer;
    sequencer.restoreState(magda::test::stateOf(state));
    const auto afterFirst = sequencer.pattern();

    sequencer.restoreState(magda::test::stateOf(state));
    CHECK(sequencer.pattern() == afterFirst);
    CHECK(afterFirst.step(0).noteNumber == 48);
    CHECK(afterFirst.step(1).noteNumber == 50);
}

TEST_CASE("A pattern the host writes restores the same pattern", "[sequencer][state]") {
    juce::ValueTree state("STEPSEQ");
    state.setProperty("seqNumSteps", 12, nullptr);
    auto glided = monoStep(2, 41);
    glided.setProperty("glide", true, nullptr);
    glided.setProperty("oct", -1, nullptr);
    state.appendChild(glided, nullptr);
    auto tied = monoStep(5, 62, false);
    tied.setProperty("tie", true, nullptr);
    state.appendChild(tied, nullptr);

    audio::StepSequencerPlugin first;
    first.restoreState(magda::test::stateOf(state));

    audio::StepSequencerPlugin second;
    second.restoreState(hostState(first.pattern()));

    CHECK(first.pattern() == second.pattern());
}

TEST_CASE("A poly step keeps its NOTE children through a state round trip",
          "[sequencer][poly][state]") {
    juce::ValueTree state("POLYSEQ");
    state.setProperty("seqNumSteps", 4, nullptr);

    juce::ValueTree step("STEP");
    step.setProperty("idx", 1, nullptr);
    step.setProperty("vel", 90, nullptr);
    step.setProperty("prob", 0.5f, nullptr);
    for (int note : {60, 64, 67}) {
        juce::ValueTree noteNode("NOTE");
        noteNode.setProperty("note", note, nullptr);
        step.appendChild(noteNode, nullptr);
    }
    // A child of another type inside a step is not a note.
    step.appendChild(juce::ValueTree("MODIFIERASSIGNMENTS"), nullptr);
    state.appendChild(step, nullptr);

    audio::PolyStepSequencerPlugin first;
    first.restoreState(magda::test::stateOf(state));

    const auto chord = first.pattern().step(1);
    REQUIRE(chord.noteCount == 3);
    CHECK(chord.notes[0].noteNumber == 60);
    CHECK(chord.notes[2].noteNumber == 67);
    CHECK(chord.velocity == 90);

    audio::PolyStepSequencerPlugin second;
    second.restoreState(hostState(first.pattern()));
    CHECK(first.pattern() == second.pattern());
}

TEST_CASE("A poly step takes the first notes it can hold and drops the rest",
          "[sequencer][poly][state]") {
    constexpr int kOverfilled = audio::PolyStepSequencerPlugin::MAX_NOTES_PER_STEP + 3;

    juce::ValueTree state("POLYSEQ");
    juce::ValueTree step("STEP");
    step.setProperty("idx", 0, nullptr);
    for (int i = 0; i < kOverfilled; ++i) {
        juce::ValueTree noteNode("NOTE");
        noteNode.setProperty("note", 36 + i, nullptr);
        step.appendChild(noteNode, nullptr);
    }
    state.appendChild(step, nullptr);

    audio::PolyStepSequencerPlugin sequencer;
    sequencer.restoreState(magda::test::stateOf(state));

    const auto chord = sequencer.pattern().step(0);
    REQUIRE(chord.noteCount == audio::PolyStepSequencerPlugin::MAX_NOTES_PER_STEP);
    CHECK(chord.notes[0].noteNumber == 36);
    CHECK(chord.notes[static_cast<size_t>(chord.noteCount) - 1].noteNumber ==
          36 + audio::PolyStepSequencerPlugin::MAX_NOTES_PER_STEP - 1);
}
