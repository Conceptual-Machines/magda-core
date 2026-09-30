#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <utility>

#include "TestDeviceMidiBuffer.hpp"
#include "core/DeviceState.hpp"
#include "magda/daw/audio/plugins/FaustInstrumentPlugin.hpp"
#include "magda/daw/audio/plugins/engine/EngineDeviceFactory.hpp"
#include "magda/daw/audio/plugins/engine/EngineMagdaDevice.hpp"

// The runtime Faust instrument's host-owned voice allocation (Poly, Mono, Legato, glide,
// pitch bend, panic), driven as the native engine drives the device (#2556).

namespace {

namespace audio = magda::daw::audio;
namespace adapter = magda::daw::audio::engine_adapter;
namespace ds = magda::device_state;
using Instrument = audio::FaustInstrumentPlugin;

// Reports the voice's own `freq` zone as its level, scaled into 0..1, so a sample says
// which pitch is sounding and how many voices are. The "stdfaust.lib" mention stops
// compile() importing a library the test binary does not stage.
constexpr const char* kVoiceModeDsp = R"FAUST(
// stdfaust.lib
freq = hslider("freq", 440, 20, 20000, 1);
gate = button("gate");

voice = (freq / 20000.0) * gate;
process = voice <: _, _;
)FAUST";

// Reports the voice's init-time sample rate, the only way to see a voice re-initialised.
constexpr const char* kSampleRateDsp = R"FAUST(
// stdfaust.lib
gate = button("gate");
SR = fconstant(int fSamplingFreq, <math.h>);
process = (SR / 96000.0) * gate <: _, _;
)FAUST";

constexpr int kBlockSize = 64;
constexpr double kSampleRate = 44100.0;
constexpr int kVoiceMode = Instrument::kVoiceModeParamIndex;
constexpr int kGlide = Instrument::kGlideParamIndex;
constexpr int kBendRange = Instrument::kBendRangeParamIndex;
constexpr float kMaxBend = Instrument::kMaxBendSemitones;
constexpr float kPoly = 0.0f;
constexpr float kMono = 0.5f;
constexpr float kLegato = 1.0f;

float expectedLevelForNote(int note) {
    const float hz = 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
    return hz / 20000.0f;
}

float expectedLevelForBentNote(int note, float semitones) {
    return expectedLevelForNote(note) * std::pow(2.0f, semitones / 12.0f);
}

std::unique_ptr<Instrument> makeInstrument(const char* source, double sampleRate = kSampleRate) {
    auto instrument = std::make_unique<Instrument>();
    juce::String error;
    const bool loaded = instrument->loadDspSource("VoiceModeTest", source, error);
    INFO(error);
    REQUIRE(loaded);
    instrument->prepare({.sampleRate = sampleRate, .maximumBlockSize = kBlockSize});
    return instrument;
}

/// Renders one block and returns its last sample, where a glide has moved furthest.
float renderBlock(Instrument& instrument, double startSeconds, magda::test::DeviceMidiBuffer midi) {
    juce::AudioBuffer<float> buffer(2, kBlockSize);
    buffer.clear();

    audio::DeviceProcessContext context;
    context.audio = &buffer;
    context.numSamples = kBlockSize;
    context.midiIn = &midi;
    magda::test::DeviceMidiBuffer out;
    context.midiOut = &out;
    context.isPlaying = true;
    context.timelineStartSeconds = startSeconds;
    context.timelineEndSeconds = startSeconds + kBlockSize / kSampleRate;
    instrument.process(context);
    return buffer.getSample(0, kBlockSize - 1);
}

magda::test::DeviceMidiBuffer midiOf(const juce::MidiMessage& message) {
    magda::test::DeviceMidiBuffer midi;
    midi.events.push_back({message.withTimeStamp(0.0), 0});
    return midi;
}

magda::test::DeviceMidiBuffer noteOn(int note) {
    return midiOf(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(127)));
}

magda::test::DeviceMidiBuffer noteOff(int note) {
    return midiOf(juce::MidiMessage::noteOff(1, note));
}

magda::test::DeviceMidiBuffer none() {
    return {};
}

// 0 is centre; +1 and -1 are the extremes of the wheel's travel.
magda::test::DeviceMidiBuffer pitchWheel(float normalised) {
    const int value = normalised < 0.0f
                          ? 8192 + static_cast<int>(std::lround(normalised * 8192.0f))
                          : 8192 + static_cast<int>(std::lround(normalised * 8191.0f));
    return midiOf(juce::MidiMessage::pitchWheel(1, value));
}

}  // namespace

TEST_CASE("Voice Mode, Glide and Bend Range exist as parameters past the pool",
          "[faust][voice-modes][2556]") {
    Instrument instrument;
    CHECK(instrument.parameterCount() > kBendRange);
    for (const int slot : {kVoiceMode, kGlide, kBendRange})
        CHECK(instrument.offersParameter(slot));
}

TEST_CASE("Poly stacks voices; Mono collapses them to one", "[faust][voice-modes][2556]") {
    auto instrument = makeInstrument(kVoiceModeDsp);
    instrument->setParameterValue(kVoiceMode, kPoly);
    instrument->setParameterValue(kGlide, 0.0f);
    instrument->reset();

    renderBlock(*instrument, 0.0, noteOn(60));
    const float polyLevel = renderBlock(*instrument, 0.1, noteOn(72));
    const float bothNotes = expectedLevelForNote(60) + expectedLevelForNote(72);
    CHECK(std::abs(polyLevel - bothNotes) <= 0.002f);

    instrument->setParameterValue(kVoiceMode, kMono);
    instrument->reset();
    renderBlock(*instrument, 0.2, noteOn(60));
    const float monoLevel = renderBlock(*instrument, 0.3, noteOn(72));
    CHECK(std::abs(monoLevel - expectedLevelForNote(72)) <= 0.002f);
    CHECK(monoLevel < bothNotes * 0.75f);
}

TEST_CASE("Releasing a Mono note falls back to the one still held", "[faust][voice-modes][2556]") {
    auto instrument = makeInstrument(kVoiceModeDsp);
    instrument->setParameterValue(kVoiceMode, kMono);
    instrument->setParameterValue(kGlide, 0.0f);
    instrument->reset();

    renderBlock(*instrument, 0.4, noteOn(60));
    renderBlock(*instrument, 0.5, noteOn(72));
    const float afterRelease = renderBlock(*instrument, 0.6, noteOff(72));
    CHECK(std::abs(afterRelease - expectedLevelForNote(60)) <= 0.002f);
}

TEST_CASE("Glide ramps between notes instead of jumping", "[faust][voice-modes][2556]") {
    auto instrument = makeInstrument(kVoiceModeDsp);
    instrument->setParameterValue(kVoiceMode, kMono);
    instrument->reset();

    instrument->setParameterValue(kGlide, 0.0f);
    renderBlock(*instrument, 0.7, noteOn(60));
    const float instant = renderBlock(*instrument, 0.8, noteOn(72));
    CHECK(std::abs(instant - expectedLevelForNote(72)) <= 0.002f);

    // 500 ms against a ~1.5 ms block: the pitch has left the old note, not reached the new.
    instrument->setParameterValue(kGlide, 0.25f);
    instrument->reset();
    renderBlock(*instrument, 0.9, noteOn(60));
    const float mid = renderBlock(*instrument, 1.0, noteOn(72));
    CHECK(mid > expectedLevelForNote(60));
    CHECK(mid < expectedLevelForNote(72) * 0.9f);
}

TEST_CASE("Switching modes silences the engine being left behind", "[faust][voice-modes][2556]") {
    // A note held in Mono gets no note-off when the mode changes, so only a flush releases it.
    auto instrument = makeInstrument(kVoiceModeDsp);
    instrument->setParameterValue(kVoiceMode, kMono);
    instrument->setParameterValue(kGlide, 0.0f);
    instrument->reset();

    CHECK(renderBlock(*instrument, 1.1, noteOn(60)) > 0.001f);

    instrument->setParameterValue(kVoiceMode, kPoly);
    CHECK(std::abs(renderBlock(*instrument, 1.2, none())) <= 0.001f);
}

TEST_CASE("Pitch bend moves the sounding pitch by the Bend Range", "[faust][voice-modes][2556]") {
    auto instrument = makeInstrument(kVoiceModeDsp);
    instrument->setParameterValue(kVoiceMode, kPoly);
    instrument->setParameterValue(kGlide, 0.0f);
    instrument->setParameterValue(kBendRange, 2.0f / kMaxBend);
    instrument->reset();

    const float unbent = renderBlock(*instrument, 1.3, noteOn(69));
    CHECK(std::abs(unbent - expectedLevelForNote(69)) <= 0.002f);

    const float bentUp = renderBlock(*instrument, 1.4, pitchWheel(1.0f));
    CHECK(std::abs(bentUp - expectedLevelForBentNote(69, 2.0f)) <= 0.002f);

    const float bentDown = renderBlock(*instrument, 1.5, pitchWheel(-1.0f));
    CHECK(std::abs(bentDown - expectedLevelForBentNote(69, -2.0f)) <= 0.002f);

    // keyOn writes an unbent freq, so a note started under a held wheel needs the re-apply.
    renderBlock(*instrument, 1.6, noteOff(69));
    const float startedBent = renderBlock(*instrument, 1.7, noteOn(69));
    CHECK(std::abs(startedBent - expectedLevelForBentNote(69, -2.0f)) <= 0.002f);

    instrument->setParameterValue(kBendRange, 12.0f / kMaxBend);
    const float octave = renderBlock(*instrument, 1.8, pitchWheel(1.0f));
    CHECK(std::abs(octave - expectedLevelForBentNote(69, 12.0f)) <= 0.004f);

    instrument->setParameterValue(kBendRange, 0.0f);
    const float inert = renderBlock(*instrument, 1.9, pitchWheel(1.0f));
    CHECK(std::abs(inert - expectedLevelForNote(69)) <= 0.002f);

    // reset() recentres the wheel: a stop or mode change delivers no wheel message.
    instrument->setParameterValue(kBendRange, 2.0f / kMaxBend);
    renderBlock(*instrument, 1.91, pitchWheel(1.0f));
    instrument->reset();
    const float unbentAgain = renderBlock(*instrument, 1.92, noteOn(69));
    CHECK(std::abs(unbentAgain - expectedLevelForNote(69)) <= 0.002f);
}

TEST_CASE("Pitch bend works in Mono, riding on the glide ramp", "[faust][voice-modes][2556]") {
    auto instrument = makeInstrument(kVoiceModeDsp);
    instrument->setParameterValue(kVoiceMode, kMono);
    instrument->setParameterValue(kGlide, 0.0f);
    instrument->setParameterValue(kBendRange, 2.0f / kMaxBend);
    instrument->reset();

    const float unbent = renderBlock(*instrument, 2.0, noteOn(69));
    CHECK(std::abs(unbent - expectedLevelForNote(69)) <= 0.002f);

    const float bent = renderBlock(*instrument, 2.1, pitchWheel(1.0f));
    CHECK(std::abs(bent - expectedLevelForBentNote(69, 2.0f)) <= 0.002f);

    const float movedBent = renderBlock(*instrument, 2.2, noteOn(72));
    CHECK(std::abs(movedBent - expectedLevelForBentNote(72, 2.0f)) <= 0.002f);
}

TEST_CASE("A duplicate note-on does not strand a pitch, in Mono or Legato (#2351)",
          "[faust][voice-modes][2556]") {
    auto instrument = makeInstrument(kVoiceModeDsp);
    instrument->setParameterValue(kGlide, 0.0f);

    double t = 2.3;
    for (const float mode : {kMono, kLegato}) {
        INFO("mode " << mode);
        instrument->setParameterValue(kVoiceMode, mode);
        instrument->reset();

        renderBlock(*instrument, t, noteOn(60));
        renderBlock(*instrument, t += 0.01, noteOn(60));
        const float afterOff = renderBlock(*instrument, t += 0.01, noteOff(60));
        t += 0.01;
        CHECK(std::abs(afterOff) <= 0.001f);
    }
}

TEST_CASE("The host's panic lets go of what is sounding, in Poly and Mono (#2722)",
          "[faust][voice-modes][2556]") {
    // The panic travels beside the events with no note-off among them (#2418).
    auto instrument = makeInstrument(kVoiceModeDsp);
    instrument->setParameterValue(kGlide, 0.0f);

    double t = 2.5;
    for (const float mode : {kPoly, kMono}) {
        INFO("mode " << mode);
        instrument->setParameterValue(kVoiceMode, mode);
        instrument->reset();

        CHECK(renderBlock(*instrument, t, noteOn(60)) > 0.001f);

        magda::test::DeviceMidiBuffer panic;
        panic.allNotesOff = true;
        const float afterPanic = renderBlock(*instrument, t += 0.01, panic);
        t += 0.01;
        CHECK(std::abs(afterPanic) <= 0.001f);
    }
}

TEST_CASE("The engine follows the device sample rate, not the provisional one",
          "[faust][voice-modes][2556]") {
    // The constructor compiles at 44.1 kHz, so only another rate tells the two apart.
    constexpr double kDeviceRate = 48000.0;
    auto instrument = makeInstrument(kSampleRateDsp, kDeviceRate);
    const float expected = static_cast<float>(kDeviceRate / 96000.0);

    instrument->setParameterValue(kVoiceMode, kPoly);
    instrument->reset();
    CHECK(std::abs(renderBlock(*instrument, 0.0, noteOn(69)) - expected) <= 0.002f);

    instrument->setParameterValue(kVoiceMode, kMono);
    instrument->reset();
    CHECK(std::abs(renderBlock(*instrument, 0.1, noteOn(69)) - expected) <= 0.002f);
}

TEST_CASE("Voice Mode, Glide and Bend Range restore from a document under their released names",
          "[faust][voice-modes][native][2556]") {
    // A value no default produces, so what comes back could only have been restored.
    constexpr float kSaved = 0.375f;
    const std::array<std::pair<const char*, int>, 3> hostParams{
        {{"voiceMode", kVoiceMode}, {"glide", kGlide}, {"bendRange", kBendRange}}};

    ds::Doc doc;
    doc.deviceType = Instrument::xmlTypeName;
    for (const auto& [name, slot] : hostParams)
        doc.root.props.set(name, kSaved);
    magda::DeviceInfo model;
    model.pluginId = Instrument::xmlTypeName;
    model.pluginState = ds::encode(doc);

    auto engineDevice = adapter::createEngineDevice(model);
    auto* hosted = dynamic_cast<adapter::EngineMagdaDevice*>(engineDevice.get());
    REQUIRE(hosted != nullptr);
    auto* restored = dynamic_cast<Instrument*>(&hosted->device());
    REQUIRE(restored != nullptr);

    juce::ValueTree flushed("PLUGIN");
    restored->flushState(flushed);
    for (const auto& [name, slot] : hostParams) {
        INFO(name);
        CHECK(restored->parameterInfo(slot).stableId == juce::String(name));
        CHECK(std::abs(restored->parameterValue(slot) - kSaved) <= 1.0e-6f);
        CHECK(std::abs(static_cast<float>(flushed.getProperty(name)) - kSaved) <= 1.0e-6f);
    }
}
