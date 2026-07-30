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
    REQUIRE(harvester.controls()[2].metadata.isMenuStyle);

    FaustParamPool pool;
    pool.rebindFromHarvest(harvester.controls());
    REQUIRE(pool.slot(2).kind == FaustParamSlot::Kind::Discrete);
}
