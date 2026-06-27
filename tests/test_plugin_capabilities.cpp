#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/PluginCapabilities.hpp"

TEST_CASE("MIDI thru toggle support follows MIDI output capability", "[plugin_capabilities]") {
    magda::DeviceInfo wrappedMidiProducer;
    wrappedMidiProducer.isInstrument = true;
    wrappedMidiProducer.deviceType = magda::DeviceType::Instrument;
    wrappedMidiProducer.producesMidi = true;

    auto wrappedCaps = magda::midiCapabilitiesForDevice(wrappedMidiProducer);
    REQUIRE(wrappedCaps.hasMidiOutput);
    REQUIRE(magda::hasMidiOutput(wrappedMidiProducer));
    REQUIRE(magda::supportsMidiSourceToggle(wrappedMidiProducer));
    REQUIRE(wrappedCaps.supportsMidiInputThruToggle);
    REQUIRE(magda::supportsMidiInputThruToggle(wrappedMidiProducer));

    magda::DeviceInfo midiFxProducer;
    midiFxProducer.isInstrument = false;
    midiFxProducer.deviceType = magda::DeviceType::MIDI;
    midiFxProducer.producesMidi = true;

    auto midiFxCaps = magda::midiCapabilitiesForDevice(midiFxProducer);
    REQUIRE(midiFxCaps.hasMidiOutput);
    REQUIRE(magda::hasMidiOutput(midiFxProducer));
    REQUIRE(magda::supportsMidiSourceToggle(midiFxProducer));
    REQUIRE(midiFxCaps.supportsMidiInputThruToggle);
    REQUIRE(magda::supportsMidiInputThruToggle(midiFxProducer));

    magda::DeviceInfo manuallyMarkedMidiFx;
    manuallyMarkedMidiFx.isInstrument = false;
    manuallyMarkedMidiFx.deviceType = magda::DeviceType::MIDI;

    auto manualCaps = magda::midiCapabilitiesForDevice(manuallyMarkedMidiFx);
    REQUIRE(manualCaps.hasMidiOutput);
    REQUIRE(magda::hasMidiOutput(manuallyMarkedMidiFx));
    REQUIRE(magda::supportsMidiSourceToggle(manuallyMarkedMidiFx));
    REQUIRE(manualCaps.supportsMidiInputThruToggle);
    REQUIRE(magda::supportsMidiInputThruToggle(manuallyMarkedMidiFx));
}

TEST_CASE("External MIDI input routing is narrower than MIDI input capability",
          "[plugin_capabilities]") {
    magda::DeviceInfo instrument;
    instrument.isInstrument = true;
    instrument.deviceType = magda::DeviceType::Instrument;
    instrument.canReceiveMidi = false;

    auto instrumentCaps = magda::midiCapabilitiesForDevice(instrument);
    REQUIRE(instrumentCaps.hasMidiInput);
    REQUIRE(magda::hasMidiInput(instrument));
    REQUIRE_FALSE(magda::supportsMidiInputRouting(instrument));
    REQUIRE_FALSE(instrumentCaps.supportsExternalMidiInputRouting);
    REQUIRE_FALSE(magda::supportsExternalMidiInputRouting(instrument));

    magda::DeviceInfo midiRoutableFx;
    midiRoutableFx.isInstrument = false;
    midiRoutableFx.deviceType = magda::DeviceType::Effect;
    midiRoutableFx.canReceiveMidi = true;

    auto fxCaps = magda::midiCapabilitiesForDevice(midiRoutableFx);
    REQUIRE(fxCaps.hasMidiInput);
    REQUIRE(magda::hasMidiInput(midiRoutableFx));
    REQUIRE(magda::supportsMidiInputRouting(midiRoutableFx));
    REQUIRE(fxCaps.supportsExternalMidiInputRouting);
    REQUIRE(magda::supportsExternalMidiInputRouting(midiRoutableFx));
    REQUIRE(magda::supportsSidechainRoutingMenu(midiRoutableFx));
}

TEST_CASE("MIDI controls are capability-based for non-instrument devices",
          "[plugin_capabilities]") {
    magda::DeviceInfo audioFxWithMidiInput;
    audioFxWithMidiInput.isInstrument = false;
    audioFxWithMidiInput.deviceType = magda::DeviceType::Effect;
    audioFxWithMidiInput.canReceiveMidi = true;

    auto audioFxCaps = magda::midiCapabilitiesForDevice(audioFxWithMidiInput);
    REQUIRE(audioFxCaps.hasMidiInput);
    REQUIRE(magda::hasMidiInput(audioFxWithMidiInput));
    REQUIRE(magda::supportsMidiInputRouting(audioFxWithMidiInput));
    REQUIRE(magda::supportsSidechainRoutingMenu(audioFxWithMidiInput));

    magda::DeviceInfo nonInstrumentMidiGenerator;
    nonInstrumentMidiGenerator.isInstrument = false;
    nonInstrumentMidiGenerator.deviceType = magda::DeviceType::Effect;
    nonInstrumentMidiGenerator.producesMidi = true;

    auto generatorCaps = magda::midiCapabilitiesForDevice(nonInstrumentMidiGenerator);
    REQUIRE(generatorCaps.hasMidiOutput);
    REQUIRE(magda::hasMidiOutput(nonInstrumentMidiGenerator));
    REQUIRE(magda::supportsMidiSourceToggle(nonInstrumentMidiGenerator));
}
