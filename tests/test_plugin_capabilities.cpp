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
