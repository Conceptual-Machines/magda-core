#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/PluginCapabilities.hpp"

TEST_CASE("MIDI thru toggle support is capability-backed and backend-aware",
          "[plugin_capabilities]") {
    magda::DeviceInfo wrappedMidiProducer;
    wrappedMidiProducer.isInstrument = true;
    wrappedMidiProducer.deviceType = magda::DeviceType::Instrument;
    wrappedMidiProducer.producesMidi = true;

    auto wrappedCaps = magda::midiCapabilitiesForDevice(wrappedMidiProducer);
    REQUIRE(wrappedCaps.hasMidiOutput);
    REQUIRE(wrappedCaps.supportsMidiInputThruToggle);
    REQUIRE(magda::supportsMidiInputThruToggle(wrappedMidiProducer));

    magda::DeviceInfo midiFxProducer;
    midiFxProducer.isInstrument = false;
    midiFxProducer.deviceType = magda::DeviceType::MIDI;
    midiFxProducer.producesMidi = true;

    auto midiFxCaps = magda::midiCapabilitiesForDevice(midiFxProducer);
    REQUIRE(midiFxCaps.hasMidiOutput);
    REQUIRE_FALSE(midiFxCaps.supportsMidiInputThruToggle);
    REQUIRE_FALSE(magda::supportsMidiInputThruToggle(midiFxProducer));
}

TEST_CASE("External MIDI input routing is narrower than MIDI input capability",
          "[plugin_capabilities]") {
    magda::DeviceInfo instrument;
    instrument.isInstrument = true;
    instrument.deviceType = magda::DeviceType::Instrument;
    instrument.canReceiveMidi = false;

    auto instrumentCaps = magda::midiCapabilitiesForDevice(instrument);
    REQUIRE(instrumentCaps.hasMidiInput);
    REQUIRE_FALSE(instrumentCaps.supportsExternalMidiInputRouting);
    REQUIRE_FALSE(magda::supportsExternalMidiInputRouting(instrument));

    magda::DeviceInfo midiRoutableFx;
    midiRoutableFx.isInstrument = false;
    midiRoutableFx.deviceType = magda::DeviceType::Effect;
    midiRoutableFx.canReceiveMidi = true;

    auto fxCaps = magda::midiCapabilitiesForDevice(midiRoutableFx);
    REQUIRE(fxCaps.hasMidiInput);
    REQUIRE(fxCaps.supportsExternalMidiInputRouting);
    REQUIRE(magda::supportsExternalMidiInputRouting(midiRoutableFx));
    REQUIRE(magda::supportsSidechainRoutingMenu(midiRoutableFx));
}
