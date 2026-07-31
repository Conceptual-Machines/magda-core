#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/audio/FaustResources.hpp"
#include "../magda/daw/audio/plugins/FaustPatchInfo.hpp"

using namespace magda::daw::audio;

// readPatchInfo scans a .dsp's global `declare` block straight out of the
// source text. It backs the credit strip under the Faust device header, so a
// patch that declares nothing has to stay silent rather than render an empty
// row, and the scan has to work on source that was never compiled.

TEST_CASE("readPatchInfo reads a full declare block", "[faust][patchinfo]") {
    const juce::String source = R"(declare name "Beat Repeater";
declare description "Host-synchronised beat repeater.";
declare author "mikobuntu";
declare license "GPL-3.0";
declare version "1.0";

import("stdfaust.lib");
process = _, _;
)";

    const auto info = readPatchInfo(source);
    CHECK(info.author == "mikobuntu");
    CHECK(info.version == "1.0");
    CHECK(info.license == "GPL-3.0");
    CHECK(info.description == "Host-synchronised beat repeater.");
    CHECK_FALSE(info.isEmpty());
}

TEST_CASE("readPatchInfo reports empty when nothing is declared", "[faust][patchinfo]") {
    // The credit strip collapses to zero height on this, so an unannotated
    // patch gives up no parameter-grid space.
    const juce::String source = R"(import("stdfaust.lib");
process = _, _;
)";

    const auto info = readPatchInfo(source);
    CHECK(info.isEmpty());
    CHECK(info.author.isEmpty());
    CHECK(info.version.isEmpty());
    CHECK(info.license.isEmpty());
    CHECK(info.description.isEmpty());
}

TEST_CASE("readPatchInfo tolerates a partial declare block", "[faust][patchinfo]") {
    // Factory DSPs carry no author. That must still count as non-empty so the
    // strip shows the license and version it does declare.
    const juce::String source = R"(declare name "MagdaReverbHall";
declare description "Zita FDN hall reverb.";
declare license "GPL-3.0";
declare version "1.0";
process = _, _;
)";

    const auto info = readPatchInfo(source);
    CHECK(info.author.isEmpty());
    CHECK(info.license == "GPL-3.0");
    CHECK(info.version == "1.0");
    CHECK_FALSE(info.isEmpty());
}

TEST_CASE("readPatchInfo works on source that does not compile", "[faust][patchinfo]") {
    // The scan is pure text, so the code editor can show a patch's credits
    // even while its DSP is mid-edit and broken.
    const juce::String source = R"(declare name "Broken";
declare author "someone";
process = this is not valid faust (((;
)";

    const auto info = readPatchInfo(source);
    CHECK(info.author == "someone");
}

TEST_CASE("readPatchInfo ignores a declare that is not one of its keys", "[faust][patchinfo]") {
    // magda_view drives the custom-view registry, not the credit strip.
    const juce::String source = R"(declare name "Drive";
declare magda_view "MagdaDrive";
process = _, _;
)";

    const auto info = readPatchInfo(source);
    CHECK(info.isEmpty());
}

// The bundled patch library is split at the top level so an instrument's Load
// menu never lists FX patches. These folder names are the staging contract
// between CMake's copy of faust_dsp/runtime and the scan at startup.

TEST_CASE("The runtime patch roots are separate per device kind", "[faust][patchinfo]") {
    const auto root = getFaustRuntimeDspPath();
    const auto effects = getFaustRuntimeDspPath(FaustPatchKind::Effect);
    const auto instruments = getFaustRuntimeDspPath(FaustPatchKind::Instrument);

    CHECK(effects.getFileName() == "effects");
    CHECK(instruments.getFileName() == "instruments");
    CHECK(effects.getParentDirectory() == root);
    CHECK(instruments.getParentDirectory() == root);
    CHECK(effects != instruments);
}

TEST_CASE("Scanning a missing patch root yields no starters", "[faust][patchinfo]") {
    // Unit tests run outside an app bundle, so nothing is staged. The scan has
    // to answer empty rather than fault.
    CHECK(getBundledStarterDsps(FaustPatchKind::Effect).empty());
    CHECK(getBundledStarterDsps(FaustPatchKind::Instrument).empty());
}
