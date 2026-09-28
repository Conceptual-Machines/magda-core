#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/DeviceInfo.hpp"

using magda::DeviceInfo;
using magda::DeviceType;

TEST_CASE("consumesMidi reads isInstrument, canReceiveMidi and MIDI type", "[core][devices]") {
    DeviceInfo instrument;
    instrument.isInstrument = true;
    REQUIRE(instrument.consumesMidi());

    DeviceInfo midiType;
    midiType.deviceType = DeviceType::MIDI;
    REQUIRE(midiType.consumesMidi());

    DeviceInfo receivingEffect;
    receivingEffect.canReceiveMidi = true;
    REQUIRE(receivingEffect.consumesMidi());

    DeviceInfo producingEffect;
    producingEffect.producesMidi = true;
    REQUIRE_FALSE(producingEffect.consumesMidi());

    DeviceInfo plainEffect;
    REQUIRE_FALSE(plainEffect.consumesMidi());
}

TEST_CASE("emitsMidi reads producesMidi and MIDI type", "[core][devices]") {
    DeviceInfo instrument;
    instrument.isInstrument = true;
    REQUIRE_FALSE(instrument.emitsMidi());

    DeviceInfo midiType;
    midiType.deviceType = DeviceType::MIDI;
    REQUIRE(midiType.emitsMidi());

    DeviceInfo receivingEffect;
    receivingEffect.canReceiveMidi = true;
    REQUIRE_FALSE(receivingEffect.emitsMidi());

    DeviceInfo producingEffect;
    producingEffect.producesMidi = true;
    REQUIRE(producingEffect.emitsMidi());

    DeviceInfo plainEffect;
    REQUIRE_FALSE(plainEffect.emitsMidi());
}
