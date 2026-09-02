#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <utility>

#include "magda/daw/core/DeviceState.hpp"
#include "magda/daw/core/StepPatternState.hpp"

// A step pattern is authored state that lives in the model's device state
// document (#2313). This is the adapter between the pattern the sequencers play
// and the `STEP` / `NOTE` elements a project file carries - a frozen
// persistence surface, since it is the retired Tracktion devices' vocabulary
// and saved projects are written in it.

namespace {

namespace sp = magda::step_pattern;
namespace ds = magda::device_state;

/// A document as it would come back from a saved project. A document with no
/// device type is not a document decode will read back, so the round-trip
/// fills one in when the test did not care.
ds::Doc encodedAndDecoded(ds::Doc doc) {
    if (doc.deviceType.isEmpty())
        doc.deviceType = "stepsequencer";
    auto decoded = ds::decode(ds::encode(doc));
    REQUIRE(decoded.has_value());
    return *decoded;
}

const juce::Identifier kStepTree("STEP");

int stepNodeCount(const ds::Doc& doc) {
    int count = 0;
    for (const auto& node : doc.root.children)
        if (node.type == kStepTree.toString())
            ++count;
    return count;
}

}  // namespace

TEST_CASE("Mono step patterns survive a document round-trip", "[steppattern]") {
    sp::MonoPattern pattern;
    pattern.length = 8;
    pattern.steps[0] = {.noteNumber = 64, .octaveShift = 1, .gate = true, .accent = true};
    pattern.steps[1] = {.noteNumber = 60, .gate = false};
    pattern.steps[3] = {.noteNumber = 67, .octaveShift = -2, .glide = true};
    pattern.steps[5] = {.noteNumber = 72, .tie = true};

    ds::Doc doc;
    doc.deviceType = "stepsequencer";
    sp::writeMono(doc, pattern);

    REQUIRE(sp::readMono(encodedAndDecoded(doc)) == pattern);
}

TEST_CASE("A mono step nobody touched is not written at all", "[steppattern]") {
    sp::MonoPattern pattern;
    pattern.length = 16;

    ds::Doc doc;
    sp::writeMono(doc, pattern);
    REQUIRE(stepNodeCount(doc) == 0);

    // Absence and a default step read back the same.
    const auto read = sp::readMono(encodedAndDecoded(doc));
    REQUIRE(read == pattern);
    REQUIRE(read.steps[0].noteNumber == 60);
    REQUIRE(read.steps[0].gate);
}

TEST_CASE("A mono pattern keeps the steps a shorter length hides", "[steppattern]") {
    sp::MonoPattern pattern;
    pattern.length = 4;
    pattern.steps[10] = {.noteNumber = 48, .gate = true, .accent = true};

    ds::Doc doc;
    sp::writeMono(doc, pattern);

    const auto read = sp::readMono(encodedAndDecoded(doc));
    REQUIRE(read.playingLength() == 4);
    REQUIRE(read.steps[10].noteNumber == 48);
    REQUIRE(read.steps[10].accent);
}

TEST_CASE("A mono pattern's length is clamped into the pattern", "[steppattern]") {
    ds::Doc doc;
    doc.root.props.set(juce::Identifier("seqNumSteps"), 999);
    REQUIRE(sp::readMono(doc).playingLength() == magda::daw::audio::sequencer::kMaxSteps);

    doc.root.props.set(juce::Identifier("seqNumSteps"), 0);
    REQUIRE(sp::readMono(doc).playingLength() == 1);
}

TEST_CASE("A mono step out of range is ignored rather than read", "[steppattern]") {
    ds::Doc doc;
    ds::Node stray;
    stray.type = kStepTree.toString();
    stray.props.set(juce::Identifier("idx"), 99);
    stray.props.set(juce::Identifier("note"), 40);
    doc.root.children.push_back(std::move(stray));

    // Nothing to assert but that it reads as an untouched pattern.
    REQUIRE(sp::readMono(doc) == sp::MonoPattern{});
}

TEST_CASE("Poly step patterns survive a document round-trip", "[steppattern]") {
    sp::PolyPattern pattern;
    pattern.length = 3;

    auto& chord = pattern.steps[0];
    chord.gate = true;
    chord.velocity = 90;
    chord.probability = 0.5f;
    chord.noteCount = 3;
    chord.notes[0] = {.noteNumber = 60};
    chord.notes[1] = {.noteNumber = 64, .velocity = 80};
    chord.notes[2] = {.noteNumber = 67};

    pattern.steps[1].tie = true;
    pattern.steps[2].gate = false;

    ds::Doc doc;
    doc.deviceType = "polystepsequencer";
    sp::writePoly(doc, pattern);

    REQUIRE(sp::readPoly(encodedAndDecoded(doc)) == pattern);
}

TEST_CASE("A poly note keeps whether it overrides the step velocity", "[steppattern]") {
    sp::PolyPattern pattern;
    pattern.length = 1;
    auto& step = pattern.steps[0];
    step.velocity = 70;
    step.noteCount = 2;
    step.notes[0] = {.noteNumber = 36};                   // takes the step's velocity
    step.notes[1] = {.noteNumber = 38, .velocity = 120};  // overrides it

    ds::Doc doc;
    sp::writePoly(doc, pattern);

    // The step that carries no override writes no `vel` property at all, which
    // is what "0 = use the step velocity" means on the way back in.
    const auto& node = doc.root.children.front();
    REQUIRE(node.children.size() == 2);
    REQUIRE(node.children[0].props.getVarPointer(juce::Identifier("vel")) == nullptr);
    REQUIRE(node.children[1].props.getVarPointer(juce::Identifier("vel")) != nullptr);

    const auto read = sp::readPoly(encodedAndDecoded(doc));
    REQUIRE(read.steps[0].notes[0].velocity == 0);
    REQUIRE(read.steps[0].notes[1].velocity == 120);
}

TEST_CASE("Writing a pattern leaves the document's other state alone", "[steppattern]") {
    ds::Doc doc;
    doc.root.props.set(juce::Identifier("seqQuantize"), 0.25f);
    ds::Node other;
    other.type = "SOMETHINGELSE";
    doc.root.children.push_back(other);

    sp::MonoPattern pattern;
    pattern.steps[0].accent = true;
    sp::writeMono(doc, pattern);

    REQUIRE(static_cast<float>(*doc.root.props.getVarPointer(juce::Identifier("seqQuantize"))) ==
            Catch::Approx(0.25f));
    bool keptOther = false;
    for (const auto& node : doc.root.children)
        keptOther |= node.type == "SOMETHINGELSE";
    REQUIRE(keptOther);
}

TEST_CASE("Rewriting a pattern replaces the steps rather than appending them", "[steppattern]") {
    ds::Doc doc;
    sp::MonoPattern first;
    first.steps[0].accent = true;
    first.steps[1].accent = true;
    sp::writeMono(doc, first);
    REQUIRE(stepNodeCount(doc) == 2);

    sp::MonoPattern second;
    second.steps[0].accent = true;
    sp::writeMono(doc, second);
    REQUIRE(stepNodeCount(doc) == 1);
    REQUIRE(sp::readMono(doc) == second);
}
