#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/audio/FaustResources.hpp"

using namespace magda::daw::audio;

// A patch opts into one of MAGDA's built-in visuals with
// `declare magda_view "Name";`. The name is read straight out of the source on
// every load, which is what lets a user's own .dsp select a view: selection is
// data in the patch, while the catalogue of views stays hardcoded C++.

TEST_CASE("readCustomViewName reads the declared view", "[faust][customview]") {
    const juce::String source = R"FAUST(
declare name "Drive";
declare magda_view "MagdaDrive";
process = _, _;
)FAUST";
    REQUIRE(readCustomViewName(source) == "MagdaDrive");
}

TEST_CASE("readCustomViewName is empty when none is declared", "[faust][customview]") {
    const juce::String source = R"FAUST(
declare name "Plain";
declare author "someone";
process = _, _;
)FAUST";
    REQUIRE(readCustomViewName(source).isEmpty());
}

TEST_CASE("readCustomViewName does not care where the source came from", "[faust][customview]") {
    // The point of the issue: a hand-written patch loaded from the file picker
    // resolves exactly like a bundled starter. Nothing here knows the origin.
    const juce::String userPatch = R"FAUST(
declare magda_view "MagdaDrive";
process = _ * 0.5, _ * 0.5;
)FAUST";
    REQUIRE(readCustomViewName(userPatch) == "MagdaDrive");
}

TEST_CASE("readCustomViewName ignores unrelated declares", "[faust][customview]") {
    const juce::String source = R"FAUST(
declare name "magda_view is not this";
declare description "mentions magda_view in prose";
process = _, _;
)FAUST";
    REQUIRE(readCustomViewName(source).isEmpty());
}

TEST_CASE("readCustomViewName survives an unknown view name", "[faust][customview]") {
    // An unregistered name is not an error: the registry returns no view and
    // the device falls back to the standard grid, so a patch written against a
    // newer MAGDA still loads and still makes sound.
    const juce::String source = R"FAUST(
declare magda_view "SomeViewFromTheFuture";
process = _, _;
)FAUST";
    REQUIRE(readCustomViewName(source) == "SomeViewFromTheFuture");
}
