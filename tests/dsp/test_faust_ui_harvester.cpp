#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/audio/plugins/FaustUIHarvester.hpp"

using namespace magda::daw::audio;

TEST_CASE("FaustUIHarvester merges group and control metadata", "[faust][harvester]") {
    FaustUIHarvester harvester;
    FAUSTFLOAT zone = 0.0f;

    harvester.openVerticalBox("Filter [unit:Hz]");
    harvester.declare(nullptr, "scale", "log");
    harvester.declare(&zone, "hidden", "1");
    harvester.declare(&zone, "role", "projectTempo");
    harvester.addHorizontalSlider("Cutoff [idx:7]", &zone, 1000.0f, 20.0f, 20000.0f, 1.0f);
    harvester.closeBox();

    REQUIRE(harvester.controls().size() == 1);
    const auto& control = harvester.controls().front();
    REQUIRE(control.label == "Cutoff");
    REQUIRE(control.group == "Filter");
    REQUIRE(control.metadata.unit == "Hz");
    REQUIRE(control.metadata.logScale);
    REQUIRE(control.metadata.hidden);
    REQUIRE(control.metadata.role == FaustControlRole::ProjectTempo);
    REQUIRE(control.metadata.slotIndex == 7);
    REQUIRE(control.zone == &zone);

    FaustParamPool pool;
    pool.rebindFromHarvest(harvester.controls());
    REQUIRE(pool.slot(7).group == "Filter");
}

TEST_CASE("FaustUIHarvester uses the outermost cleaned group", "[faust][harvester]") {
    FaustUIHarvester harvester;
    FAUSTFLOAT zone = 0.0f;

    harvester.openTabBox("Tone [hidden:1]");
    harvester.openHorizontalBox("Advanced");
    harvester.addNumEntry("Drive", &zone, 0.5f, 0.0f, 1.0f, 0.01f);
    harvester.closeBox();
    harvester.closeBox();

    REQUIRE(harvester.controls().front().group == "Tone");
    REQUIRE(harvester.controls().front().metadata.hidden);
}

TEST_CASE("FaustUIHarvester poly mode skips proxy controls and keeps author groups",
          "[faust][harvester]") {
    FaustUIHarvester harvester(FaustUIHarvester::Layout::PolyphonicVoices);
    FAUSTFLOAT proxyZone = 0.0f;
    FAUSTFLOAT voiceZone = 0.0f;

    harvester.openTabBox("Polyphonic");
    harvester.openHorizontalBox("Voices");
    harvester.openVerticalBox("Filter");
    harvester.addHorizontalSlider("Cutoff [idx:3]", &proxyZone, 1000.0f, 20.0f, 20000.0f, 1.0f);
    harvester.closeBox();
    harvester.closeBox();

    harvester.openVerticalBox("Voice1");
    harvester.openVerticalBox("Filter");
    harvester.addHorizontalSlider("Cutoff [idx:3]", &voiceZone, 1000.0f, 20.0f, 20000.0f, 1.0f);
    harvester.closeBox();
    harvester.closeBox();
    harvester.closeBox();

    REQUIRE(harvester.controls().size() == 1);
    REQUIRE(harvester.controls().front().zone == &voiceZone);
    REQUIRE(harvester.controls().front().group == "Filter");
    REQUIRE(harvester.controls().front().metadata.slotIndex == 3);
}

TEST_CASE("FaustUIHarvester maps active widgets to shared control kinds", "[faust][harvester]") {
    FaustUIHarvester harvester;
    FAUSTFLOAT triggerZone = 0.0f;
    FAUSTFLOAT booleanZone = 0.0f;
    FAUSTFLOAT continuousZone = 0.0f;

    harvester.addButton("Trigger", &triggerZone);
    harvester.addCheckButton("Enabled", &booleanZone);
    harvester.addVerticalSlider("Mode [style:menu{'Off':0;'On':1}]", &continuousZone, 0.0f, 0.0f,
                                1.0f, 1.0f);

    REQUIRE(harvester.controls().size() == 3);
    REQUIRE(harvester.controls()[0].kind == FaustParamSlot::Kind::Trigger);
    REQUIRE(harvester.controls()[1].kind == FaustParamSlot::Kind::Boolean);
    REQUIRE(harvester.controls()[2].kind == FaustParamSlot::Kind::Continuous);
    REQUIRE(harvester.controls()[2].metadata.isChoiceStyle());

    FaustParamPool pool;
    pool.rebindFromHarvest(harvester.controls());
    REQUIRE(pool.slot(2).kind == FaustParamSlot::Kind::Discrete);
}

// ============================================================================
// Bargraphs
// ============================================================================

TEST_CASE("FaustUIHarvester harvests bargraphs into the output list", "[faust][harvester]") {
    FaustUIHarvester harvester;
    FAUSTFLOAT sliderZone = 0.0f;
    FAUSTFLOAT meterZone = 0.0f;

    harvester.openVerticalBox("Dynamics");
    harvester.addHorizontalSlider("Threshold [idx:0]", &sliderZone, -18.0f, -60.0f, 0.0f, 0.1f);
    harvester.addHorizontalBargraph("GR [unit:dB] [width:2] [tooltip:Gain reduction]", &meterZone,
                                    0.0f, 24.0f);
    harvester.closeBox();

    // An output must never show up as a control: the whole point of the second
    // list is that nothing binding-related can reach it.
    REQUIRE(harvester.controls().size() == 1);
    REQUIRE(harvester.outputs().size() == 1);

    const auto& output = harvester.outputs().front();
    REQUIRE(output.label == "GR");
    REQUIRE(output.group == "Dynamics");
    REQUIRE(output.metadata.unit == "dB");
    REQUIRE(output.metadata.widthCells == 2);
    REQUIRE(output.metadata.tooltip == "Gain reduction");
    REQUIRE_FALSE(output.vertical);
    REQUIRE(output.minValue == 0.0f);
    REQUIRE(output.maxValue == 24.0f);
    REQUIRE(output.zones.size() == 1);
    REQUIRE(output.zones.front() == &meterZone);
}

TEST_CASE("FaustUIHarvester reads bargraph style hints", "[faust][harvester]") {
    FaustUIHarvester harvester;
    FAUSTFLOAT led = 0.0f;
    FAUSTFLOAT numeric = 0.0f;
    FAUSTFLOAT plain = 0.0f;

    harvester.addVerticalBargraph("Clip [style:led]", &led, 0.0f, 1.0f);
    harvester.addHorizontalBargraph("Pitch [style:numerical]", &numeric, 0.0f, 127.0f);
    harvester.addVerticalBargraph("Band", &plain, -60.0f, 0.0f);

    REQUIRE(harvester.outputs().size() == 3);
    REQUIRE(harvester.outputs()[0].metadata.outputStyle == FaustOutputStyle::Led);
    REQUIRE(harvester.outputs()[0].vertical);
    REQUIRE(harvester.outputs()[1].metadata.outputStyle == FaustOutputStyle::Numerical);
    REQUIRE_FALSE(harvester.outputs()[1].vertical);
    REQUIRE(harvester.outputs()[2].metadata.outputStyle == FaustOutputStyle::Bar);
    REQUIRE(harvester.outputs()[2].label == "Band");
}

TEST_CASE("FaustUIHarvester poly mode skips proxy bargraphs", "[faust][harvester]") {
    FaustUIHarvester harvester(FaustUIHarvester::Layout::PolyphonicVoices);
    FAUSTFLOAT proxyZone = 0.0f;
    FAUSTFLOAT voiceZone = 0.0f;

    harvester.openTabBox("Polyphonic");
    harvester.openHorizontalBox("Voices");
    harvester.addHorizontalBargraph("Level", &proxyZone, 0.0f, 1.0f);
    harvester.closeBox();
    harvester.openHorizontalBox("Voice0");
    harvester.openVerticalBox("Amp");
    harvester.addHorizontalBargraph("Level", &voiceZone, 0.0f, 1.0f);
    harvester.closeBox();
    harvester.closeBox();
    harvester.closeBox();

    REQUIRE(harvester.outputs().size() == 1);
    REQUIRE(harvester.outputs().front().zones.front() == &voiceZone);
    REQUIRE(harvester.outputs().front().group == "Amp");
}
