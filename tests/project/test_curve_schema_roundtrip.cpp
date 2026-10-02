#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>
#include <support/CurveCorpus.hpp>
#include <support/CurveGolden.hpp>
#include <support/CurveHashes.hpp>

#include "magda/daw/core/AutomationInfo.hpp"
#include "magda/daw/core/ModInfo.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"
#include "magda/sdk/curve/Curve.hpp"

// Every curve a project saves reads back through the SDK curve schema and evaluates as before.
// The golden hashes are over the corpus values themselves: a project writes doubles with fewer
// digits than a double needs, so a loaded value is not always the one that was saved.

using namespace magda;
using namespace magda::curvecorpus;

namespace {

juce::String savedForm(const juce::var& value) {
    return juce::JSON::toString(value, false);
}

juce::var parseSaved(const juce::String& text) {
    juce::var parsed;
    REQUIRE(juce::JSON::parse(text, parsed).wasOk());
    return parsed;
}

std::vector<CurvePointData> throughSchema(const std::vector<CurvePointData>& points) {
    sdk::Curve curve;
    curve.points = points;
    std::string error;
    const auto text = sdk::writeCurve(curve, error);
    INFO(error);
    REQUIRE(text.has_value());
    const auto read = sdk::readCurve(*text);
    INFO(read.message);
    REQUIRE(read.ok());
    return std::get<std::vector<CurvePointData>>(read.curve->points);
}

std::vector<AutomationPoint> throughSchema(const std::vector<AutomationPoint>& points) {
    sdk::Curve curve;
    curve.points = points;
    std::string error;
    const auto text = sdk::writeCurve(curve, error);
    INFO(error);
    REQUIRE(text.has_value());
    const auto read = sdk::readCurve(*text);
    INFO(read.message);
    REQUIRE(read.ok());
    return std::get<std::vector<AutomationPoint>>(read.curve->points);
}

ModInfo savedMod(const PhaseCase& c) {
    ModInfo mod;
    mod.waveform = LFOWaveform::Custom;
    mod.curvePreset = CurvePreset::SCurve;
    mod.curvePoints = c.points;
    mod.invertOutput = c.name == "sidechain_duck";
    return mod;
}

}  // namespace

TEST_CASE("an LFO or sidechain curve a project saves reads through the curve schema losslessly",
          "[curve][serialization]") {
    for (const auto& c : phaseCases()) {
        INFO(c.name);
        const auto saved = savedForm(ProjectSerializer::serializeModInfo(savedMod(c)));

        ModInfo loaded;
        REQUIRE(ProjectSerializer::deserializeModInfo(parseSaved(saved), loaded));

        ModInfo viaSchema = loaded;
        viaSchema.curvePoints = throughSchema(loaded.curvePoints);

        CHECK(viaSchema.curvePoints == loaded.curvePoints);
        CHECK(savedForm(ProjectSerializer::serializeModInfo(viaSchema)) ==
              savedForm(ProjectSerializer::serializeModInfo(loaded)));
        CHECK(savedForm(ProjectSerializer::serializeModInfo(loaded)) == saved);

        Hashes direct;
        Hashes through;
        hashPhaseCase(direct, {c.name, loaded.curvePoints});
        hashPhaseCase(through, {c.name, viaSchema.curvePoints});
        CHECK(through == direct);
    }
}

TEST_CASE("an automation lane or clip a project saves reads through the curve schema losslessly",
          "[curve][serialization]") {
    for (const auto& c : beatCases()) {
        INFO(c.name);

        AutomationLaneInfo lane;
        lane.absolutePoints = c.points;
        AutomationClipInfo clip;
        clip.points = c.points;
        clip.startBeats = 8.0;
        clip.looping = true;

        const auto savedLane = savedForm(ProjectSerializer::serializeAutomationLaneInfo(lane));
        const auto savedClip = savedForm(ProjectSerializer::serializeAutomationClipInfo(clip));

        AutomationLaneInfo loadedLane;
        AutomationClipInfo loadedClip;
        REQUIRE(
            ProjectSerializer::deserializeAutomationLaneInfo(parseSaved(savedLane), loadedLane));
        REQUIRE(
            ProjectSerializer::deserializeAutomationClipInfo(parseSaved(savedClip), loadedClip));

        AutomationLaneInfo laneViaSchema = loadedLane;
        laneViaSchema.absolutePoints = throughSchema(loadedLane.absolutePoints);
        AutomationClipInfo clipViaSchema = loadedClip;
        clipViaSchema.points = throughSchema(loadedClip.points);

        CHECK(savedForm(ProjectSerializer::serializeAutomationLaneInfo(laneViaSchema)) ==
              savedLane);
        CHECK(savedForm(ProjectSerializer::serializeAutomationClipInfo(clipViaSchema)) ==
              savedClip);

        Hashes direct;
        Hashes through;
        hashBeatCase(direct, {c.name, loadedLane.absolutePoints});
        hashBeatCase(through, {c.name, laneViaSchema.absolutePoints});
        CHECK(through == direct);
    }
}

TEST_CASE(
    "the corpus evaluates through the curve schema as the pre-move evaluators did, bit for bit",
    "[curve][golden]") {
    Hashes got;
    Hashes direct;
    for (const auto& c : phaseCases()) {
        hashPhaseCase(got, {c.name, throughSchema(c.points)});
        hashPhaseCase(direct, {c.name, c.points});
    }
    for (const auto& c : beatCases()) {
        hashBeatCase(got, {c.name, throughSchema(c.points)});
        hashBeatCase(direct, {c.name, c.points});
    }
    for (const auto& c : laneCases())
        hashLaneCase(got, c);
    for (const auto& c : simplifyCases())
        hashSimplifyCase(got, c);
    hashBuiltIns(got);

    CHECK(got.size() == goldenHashes().size());
    for (const auto& [key, hash] : direct) {
        INFO(key);
        CHECK(got.at(key) == hash);
    }
#if defined(__APPLE__) && defined(__aarch64__)
    // Captured on this platform; libm and FMA contraction differ elsewhere.
    for (const auto& [key, hash] : got) {
        INFO(key);
        CHECK(hash == goldenHashes().at(key));
    }
#endif
}
