#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include "SharedTestEngine.hpp"
#include "magda/daw/audio/plugins/FaustInstrumentPlugin.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"

namespace {

namespace audio = magda::daw::audio;
namespace te = tracktion::engine;

constexpr const char* kTempoInstrumentDsp = R"FAUST(
// Self-contained test DSP. The literal "stdfaust.lib" in this comment is
// load-bearing: compile() only skips its automatic import when the source
// already mentions the library, and the test binary has no faustlibraries dir.
freq = hslider("freq", 440, 20, 20000, 1);
gain = hslider("gain", 0.5, 0, 1, 0.01);
gate = button("gate");
tempo = hslider("tempo [role:projectTempo] [hidden:1]", 120, 20, 300, 0.01);

voice = (tempo / 300.0) * gain * gate;
process = voice <: _, _;
)FAUST";

te::Plugin::Ptr createFaustInstrument(te::Edit& edit) {
    juce::ValueTree state(te::IDs::PLUGIN);
    state.setProperty(te::IDs::type, audio::FaustInstrumentPlugin::xmlTypeName, nullptr);
    return edit.getPluginCache().createNewPlugin(state);
}

float renderBlock(audio::FaustInstrumentPlugin& instrument, double startSeconds,
                  te::MidiMessageArray& midi) {
    constexpr int kBlockSize = 64;
    juce::AudioBuffer<float> buffer(2, kBlockSize);
    buffer.clear();

    te::PluginRenderContext context(
        &buffer, juce::AudioChannelSet::stereo(), 0, kBlockSize, &midi, 0.0,
        tracktion::TimeRange(
            tracktion::TimePosition::fromSeconds(startSeconds),
            tracktion::TimePosition::fromSeconds(startSeconds + kBlockSize / 44100.0)),
        true, false, false, false);
    instrument.applyToBuffer(context);
    return buffer.getSample(0, kBlockSize - 1);
}

class FaustInstrumentTempoTest final : public juce::UnitTest {
  public:
    FaustInstrumentTempoTest() : juce::UnitTest("Faust Instrument Project Tempo Tests", "magda") {}

    void runTest() override {
        beginTest("Project tempo is written to every runtime Faust instrument voice");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr, "Test edit should be created");
        if (!edit)
            return;

        auto plugin = createFaustInstrument(*edit);
        auto* instrument = dynamic_cast<audio::FaustInstrumentPlugin*>(plugin.get());
        expect(instrument != nullptr, "Runtime Faust instrument should be created");
        if (!instrument)
            return;

        juce::String error;
        const bool loaded = instrument->loadDspSource("Tempo test", kTempoInstrumentDsp, error);
        expect(loaded, "Tempo test DSP should compile: " + error);
        if (!loaded)
            return;

        te::PluginInitialisationInfo initInfo;
        initInfo.startTime = tracktion::TimePosition();
        initInfo.sampleRate = 44100.0;
        initInfo.blockSizeSamples = 64;
        instrument->baseClassInitialise(initInfo);

        auto* firstTempo = edit->tempoSequence.getTempo(0);
        expect(firstTempo != nullptr, "Test edit should have an initial tempo");
        if (!firstTempo)
            return;
        firstTempo->setBpm(90.0);

        te::MidiMessageArray noteOns;
        noteOns.addMidiMessage(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(127)), 0.0,
                               te::MPESourceID{});
        noteOns.addMidiMessage(juce::MidiMessage::noteOn(1, 67, static_cast<juce::uint8>(127)), 0.0,
                               te::MPESourceID{});
        const float at90Bpm = renderBlock(*instrument, 0.0, noteOns);

        firstTempo->setBpm(180.0);
        te::MidiMessageArray noMidi;
        const float at180Bpm = renderBlock(*instrument, 1.0, noMidi);

        expectWithinAbsoluteError(at90Bpm, 0.6f, 0.0001f,
                                  "Two voices should each render (90 / 300) at full velocity");
        expectWithinAbsoluteError(at180Bpm, at90Bpm * 2.0f, 0.0001f,
                                  "Doubling project tempo should update every active voice");
    }
};

FaustInstrumentTempoTest faustInstrumentTempoTest;

}  // namespace
