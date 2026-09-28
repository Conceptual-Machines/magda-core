#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "clip/GrooveTemplate.hpp"

/**
 * The native groove template (#2039).
 *
 * A port of the fork's, so the tests are about the formula agreeing with it
 * rather than about a shape anybody chose: a swing template has to move an
 * off-beat late by the amount the stored table says, and a clip saved under the
 * fork has to keep playing the way it did.
 */

using Catch::Approx;
using magda::engine::GrooveTemplate;
using magda::engine::GrooveTemplateSet;

namespace {

/// The fork's own "Basic 8th Swing": two steps, two per beat, parameterized.
GrooveTemplate eighthSwing(float strength) {
    return GrooveTemplate::compile({0.0f, 0.66f}, 2, 2, true, strength);
}

}  // namespace

TEST_CASE("An empty template is the identity", "[engine][clip][groove]") {
    const GrooveTemplate none;
    CHECK(none.empty());
    CHECK(none.groovyBeat(0.0) == Approx(0.0));
    CHECK(none.groovyBeat(3.25) == Approx(3.25));
    CHECK(none.maxDisplacementBeats() == Approx(0.0));
}

TEST_CASE("A table of zeroes compiles to nothing", "[engine][clip][groove]") {
    // Not merely harmless: making it the identity is what lets every caller
    // downstream be groove-agnostic rather than groove-aware-and-checking.
    const auto groove = GrooveTemplate::compile({0.0f, 0.0f, 0.0f}, 3, 2, false, 1.0f);
    CHECK(groove.empty());
}

TEST_CASE("Swing leaves the beat and moves the off-beat", "[engine][clip][groove]") {
    const auto groove = eighthSwing(1.0f);

    // The downbeats sit on grid steps whose own lateness is zero, so they do
    // not move.
    CHECK(groove.groovyBeat(0.0) == Approx(0.0));
    CHECK(groove.groovyBeat(1.0) == Approx(0.0 + 1.0).margin(1e-9));

    // The eighth between them is a whole grid step late by half the stored
    // 0.66, over a two-per-beat grid.
    CHECK(groove.groovyBeat(0.5) == Approx(0.5 + 0.5 * 0.66 / 2.0));
}

TEST_CASE("Only a parameterized template answers to strength", "[engine][clip][groove]") {
    const auto full = eighthSwing(1.0f);
    const auto half = eighthSwing(0.5f);

    CHECK(half.groovyBeat(0.5) - 0.5 == Approx((full.groovyBeat(0.5) - 0.5) * 0.5));

    // The fork's getLatenessProportion ignores strength when the template is
    // not parameterized, so a clip's slider has nothing to say about it.
    const auto fixed = GrooveTemplate::compile({0.0f, 0.66f}, 2, 2, false, 0.5f);
    CHECK(fixed.groovyBeat(0.5) == Approx(full.groovyBeat(0.5)));
}

TEST_CASE("The displacement bound holds everywhere", "[engine][clip][groove]") {
    const auto groove = GrooveTemplate::compile({0.0f, 1.0f, -0.4f, 0.7f}, 4, 4, false, 1.0f);
    const auto bound = groove.maxDisplacementBeats();

    // What the block's event search widens by. A bound that is not a bound
    // loses events the groove moved into the block.
    for (auto beat = 0.0; beat < 8.0; beat += 1.0 / 64.0)
        CHECK(std::abs(groove.groovyBeat(beat) - beat) <= bound + 1e-9);

    CHECK(bound == Approx(0.5 * 1.0 / 4.0));
}

TEST_CASE("The pattern repeats and is anchored at beat zero", "[engine][clip][groove]") {
    const auto groove = eighthSwing(1.0f);

    // Two steps at two per beat is a one-beat pattern, so the same position one
    // beat later is displaced identically. This is what makes groove a property
    // of the project grid rather than of the clip.
    for (auto beat = 0.0; beat < 1.0; beat += 1.0 / 32.0) {
        const auto here = groove.groovyBeat(beat) - beat;
        const auto later = groove.groovyBeat(beat + 1.0) - (beat + 1.0);
        CHECK(here == Approx(later).margin(1e-9));
    }
}

TEST_CASE("Latenesses past the table read as zero", "[engine][clip][groove]") {
    // The fork reads its table through a juce::Array, which answers zero past
    // the end, so a clip saved with fewer shifts than notes has to play alike.
    const auto shortTable = GrooveTemplate::compile({0.5f}, 4, 2, false, 1.0f);
    const auto padded = GrooveTemplate::compile({0.5f, 0.0f, 0.0f, 0.0f}, 4, 2, false, 1.0f);

    for (auto beat = 0.0; beat < 4.0; beat += 1.0 / 16.0)
        CHECK(shortTable.groovyBeat(beat) == Approx(padded.groovyBeat(beat)));
}

TEST_CASE("A set parses the fork's own document", "[engine][clip][groove]") {
    const auto* xml =
        "<GROOVETEMPLATES>"
        "<GROOVETEMPLATE name=\"Basic 8th Swing\" numberOfNotes=\"2\" notesPerBeat=\"2\" "
        "parameterized=\"1\"><SHIFT delta=\"0.0\"/><SHIFT delta=\"0.66\"/></GROOVETEMPLATE>"
        "</GROOVETEMPLATES>";

    const auto document = juce::parseXML(juce::String(xml));
    REQUIRE(document != nullptr);

    const auto set = GrooveTemplateSet::parse(*document);
    REQUIRE(set.size() == 1);
    CHECK(set.contains("Basic 8th Swing"));

    const auto groove = set.compile("Basic 8th Swing", 1.0f);
    CHECK(groove.groovyBeat(0.5) == Approx(eighthSwing(1.0f).groovyBeat(0.5)));

    // Naming a template this installation does not have is an ordinary answer
    // rather than a failure: a project can travel further than a settings file.
    CHECK(set.compile("Nothing Like It", 1.0f).empty());
    CHECK(set.compile("", 1.0f).empty());
}
