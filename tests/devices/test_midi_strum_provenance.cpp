#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <initializer_list>

#include "TestDeviceMidiBuffer.hpp"
#include "magda/daw/audio/plugins/MidiStrumPlugin.hpp"

namespace {
namespace audio = magda::daw::audio;

magda::test::DeviceMidiBuffer runBlock(audio::MidiStrumPlugin& strum,
                                       std::initializer_list<juce::MidiMessage> input,
                                       std::uint32_t source) {
    magda::test::DeviceMidiBuffer midi;
    for (const auto& message : input)
        midi.events.push_back({message, source});
    audio::DeviceProcessContext context;
    context.midi = &midi;
    context.numSamples = 2400;
    context.isPlaying = true;
    strum.process(context);
    return midi;
}
}  // namespace

TEST_CASE("Strum hands on the provenance of the chord it was given", "[strum][midi]") {
    audio::MidiStrumPlugin strum;
    strum.prepare({.sampleRate = 48000.0, .maximumBlockSize = 2400});

    constexpr std::uint32_t kSource = 77;
    int strummed = 0;
    // The collect window closes inside the first block; the onsets are spread
    // over the strum length, so give them a few blocks to all land.
    for (int block = 0; block < 4; ++block) {
        const auto midi =
            block == 0
                ? runBlock(strum, {juce::MidiMessage::noteOn(1, 60, juce::uint8{91})}, kSource)
                : runBlock(strum, {}, 0);
        for (int i = 0; i < midi.size(); ++i) {
            INFO("block " << block << " event " << i);
            CHECK(midi.events[static_cast<size_t>(i)].sourceId == kSource);
            ++strummed;
        }
    }
    CHECK(strummed > 0);
}
