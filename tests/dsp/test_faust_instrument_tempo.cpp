#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "DeviceTestBlock.hpp"
#include "TestDeviceMidiBuffer.hpp"
#include "magda/daw/audio/plugins/FaustInstrumentPlugin.hpp"

// A runtime Faust instrument's `[role:projectTempo]` control follows the tempo map the
// native engine hands the device (#2556).

namespace {

namespace audio = magda::daw::audio;

// The "stdfaust.lib" mention stops compile() importing a library the binary does not stage.
constexpr const char* kTempoInstrumentDsp = R"FAUST(
// stdfaust.lib
freq = hslider("freq", 440, 20, 20000, 1);
gain = hslider("gain", 0.5, 0, 1, 0.01);
gate = button("gate");
tempo = hslider("tempo [role:projectTempo] [hidden:1]", 120, 20, 300, 0.01);

voice = (tempo / 300.0) * gain * gate;
process = voice <: _, _;
)FAUST";

constexpr int kBlockSize = 64;
constexpr double kSampleRate = 44100.0;

class ConstantTempo final : public magda::sdk::TempoMap {
  public:
    double beatsAtSeconds(double seconds) const override {
        return seconds * bpm / 60.0;
    }
    double bpmAtSeconds(double) const override {
        return bpm;
    }

    double bpm = 120.0;
};

float renderBlock(audio::MagdaDevice& device, const magda::sdk::TempoMap& tempo,
                  double startSeconds, magda::test::DeviceMidiBuffer& midi) {
    juce::AudioBuffer<float> buffer(2, kBlockSize);
    buffer.clear();

    magda::test::DeviceMidiBuffer out;
    magda::test::DeviceTestBlock contextBlock(buffer, kBlockSize);
    auto& context = contextBlock.context;
    context.midiIn = &midi;
    context.midiOut = &out;
    context.tempoMap = &tempo;
    context.isPlaying = true;
    context.timelineStartSeconds = startSeconds;
    context.timelineEndSeconds = startSeconds + kBlockSize / kSampleRate;
    device.process(context);
    return buffer.getSample(0, kBlockSize - 1);
}

}  // namespace

TEST_CASE("Project tempo is written to every runtime Faust instrument voice",
          "[faust][tempo][2556]") {
    audio::FaustInstrumentPlugin instrument;
    juce::String error;
    const bool loaded = instrument.loadDspSource("Tempo test", kTempoInstrumentDsp, error);
    INFO(error);
    REQUIRE(loaded);
    instrument.prepare({.sampleRate = kSampleRate, .maximumBlockSize = kBlockSize});

    ConstantTempo tempo;
    tempo.bpm = 90.0;
    magda::test::DeviceMidiBuffer noteOns;
    noteOns.events.push_back({juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(127)), 0});
    noteOns.events.push_back({juce::MidiMessage::noteOn(1, 67, static_cast<juce::uint8>(127)), 0});
    const float at90Bpm = renderBlock(instrument, tempo, 0.0, noteOns);

    tempo.bpm = 180.0;
    magda::test::DeviceMidiBuffer noMidi;
    const float at180Bpm = renderBlock(instrument, tempo, 1.0, noMidi);

    // Two voices, each (90 / 300) at full velocity; doubling the tempo doubles both.
    CHECK(std::abs(at90Bpm - 0.6f) <= 0.0001f);
    CHECK(std::abs(at180Bpm - at90Bpm * 2.0f) <= 0.0001f);
}
