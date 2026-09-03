#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <cmath>

#include "SharedTestEngine.hpp"
#include "magda/daw/audio/plugins/FaustInstrumentPlugin.hpp"
#include "magda/daw/audio/plugins/tracktion/TracktionMagdaDevicePlugin.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"

namespace {

namespace audio = magda::daw::audio;
namespace te = tracktion::engine;

// Reports the voice's own `freq` zone as its output level, scaled into 0..1, so
// a rendered sample says directly which pitch that voice is playing and how
// many voices are sounding. Gated so an ungated voice contributes nothing.
constexpr const char* kVoiceModeDsp = R"FAUST(
// The literal "stdfaust.lib" in this comment is load-bearing, and has to sit
// inside the DSP source rather than beside it in C++: compile() only skips its
// automatic import when the source already mentions the library, and the test
// binary has no faustlibraries dir to satisfy that import from.
freq = hslider("freq", 440, 20, 20000, 1);
gate = button("gate");

voice = (freq / 20000.0) * gate;
process = voice <: _, _;
)FAUST";

constexpr int kBlockSize = 64;
constexpr double kSampleRate = 44100.0;

// The scale kVoiceModeDsp applies, so a test can say "expect note 69" rather
// than "expect 0.022".
float expectedLevelForNote(int note) {
    const float hz = 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
    return hz / 20000.0f;
}

te::Plugin::Ptr createFaustInstrument(te::Edit& edit) {
    juce::ValueTree state(te::IDs::PLUGIN);
    state.setProperty(te::IDs::type, audio::FaustInstrumentPlugin::xmlTypeName, nullptr);
    return edit.getPluginCache().createNewPlugin(state);
}

void setHostParam(te::Plugin& plugin, int paramIndex, float normalised) {
    auto params = plugin.getAutomatableParameters();
    if (paramIndex < params.size() && params[paramIndex])
        params[paramIndex]->setParameterFromHost(normalised, juce::sendNotificationSync);
}

// Renders one block and returns its final sample. Taking the last sample rather
// than the first matters for the glide tests: the ramp advances across the
// block, so the end is where movement has actually happened.
float renderBlock(te::Plugin& plugin, double startSeconds, te::MidiMessageArray& midi) {
    juce::AudioBuffer<float> buffer(2, kBlockSize);
    buffer.clear();

    te::PluginRenderContext context(
        &buffer, juce::AudioChannelSet::stereo(), 0, kBlockSize, &midi, 0.0,
        tracktion::TimeRange(
            tracktion::TimePosition::fromSeconds(startSeconds),
            tracktion::TimePosition::fromSeconds(startSeconds + kBlockSize / kSampleRate)),
        true, false, false, false);
    plugin.applyToBuffer(context);
    return buffer.getSample(0, kBlockSize - 1);
}

te::MidiMessageArray noteOn(int note) {
    te::MidiMessageArray midi;
    midi.addMidiMessage(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(127)), 0.0,
                        te::MidiMessageArray::notMPE);
    return midi;
}

te::MidiMessageArray noteOff(int note) {
    te::MidiMessageArray midi;
    midi.addMidiMessage(juce::MidiMessage::noteOff(1, note), 0.0, te::MidiMessageArray::notMPE);
    return midi;
}

// 0 is centre; +1 and -1 are the extremes of the wheel's travel.
te::MidiMessageArray pitchWheel(float normalised) {
    const int value = normalised < 0.0f
                          ? 8192 + static_cast<int>(std::lround(normalised * 8192.0f))
                          : 8192 + static_cast<int>(std::lround(normalised * 8191.0f));
    te::MidiMessageArray midi;
    midi.addMidiMessage(juce::MidiMessage::pitchWheel(1, value), 0.0, te::MidiMessageArray::notMPE);
    return midi;
}

// The pitch a note bends to, as the DSP's scaled level.
float expectedLevelForBentNote(int note, float semitones) {
    const float hz = 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
    return hz * std::pow(2.0f, semitones / 12.0f) / 20000.0f;
}

class FaustInstrumentVoiceModeTest final : public juce::UnitTest {
  public:
    FaustInstrumentVoiceModeTest() : juce::UnitTest("Faust Instrument Voice Mode Tests", "magda") {}

    void runTest() override {
        // JUCE asserts if anything is expect()ed before a test has been begun,
        // so the setup lives under its own heading rather than ahead of them.
        beginTest("Voice Mode and Glide exist as host parameters past the pool");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr, "Test edit should be created");
        if (!edit)
            return;

        auto plugin = createFaustInstrument(*edit);
        // The device inside the host wrapper (#2315).
        auto* instrument =
            audio::tracktion_adapter::deviceFromPlugin<audio::FaustInstrumentPlugin>(plugin.get());
        expect(instrument != nullptr, "Faust instrument should be created");
        if (instrument == nullptr)
            return;

        juce::String err;
        expect(instrument->loadDspSource("VoiceModeTest", kVoiceModeDsp, err),
               "Test DSP should compile: " + err);

        te::PluginInitialisationInfo initInfo;
        initInfo.startTime = tracktion::TimePosition();
        initInfo.sampleRate = kSampleRate;
        initInfo.blockSizeSamples = kBlockSize;
        plugin->baseClassInitialise(initInfo);

        const int voiceModeIdx = audio::FaustInstrumentPlugin::kVoiceModeParamIndex;
        const int glideIdx = audio::FaustInstrumentPlugin::kGlideParamIndex;

        {
            auto params = plugin->getAutomatableParameters();
            expect(params.size() > audio::FaustInstrumentPlugin::kBendRangeParamIndex,
                   "Instrument should expose three parameters beyond the 64-slot pool");
        }

        beginTest("Poly stacks voices; Mono collapses them to one");
        {
            // Poly is the default. Two held notes should sound together, so the
            // level is the sum of both - the point of the mode.
            setHostParam(*plugin, voiceModeIdx, 0.0f);
            setHostParam(*plugin, glideIdx, 0.0f);
            instrument->reset();

            auto first = noteOn(60);
            renderBlock(*plugin, 0.0, first);
            auto second = noteOn(72);
            const float polyLevel = renderBlock(*plugin, 0.1, second);

            const float bothNotes = expectedLevelForNote(60) + expectedLevelForNote(72);
            expectWithinAbsoluteError(polyLevel, bothNotes, 0.002f);

            // Mono: the same two notes leave only the newest sounding.
            setHostParam(*plugin, voiceModeIdx, 0.5f);  // 0.5 * 2 == Mono
            instrument->reset();
            auto monoFirst = noteOn(60);
            renderBlock(*plugin, 0.2, monoFirst);
            auto monoSecond = noteOn(72);
            const float monoLevel = renderBlock(*plugin, 0.3, monoSecond);

            expectWithinAbsoluteError(monoLevel, expectedLevelForNote(72), 0.002f);
            expect(monoLevel < bothNotes * 0.75f, "Mono should not stack the two notes");
        }

        beginTest("Releasing a Mono note falls back to the one still held");
        {
            setHostParam(*plugin, voiceModeIdx, 0.5f);
            setHostParam(*plugin, glideIdx, 0.0f);
            instrument->reset();

            auto low = noteOn(60);
            renderBlock(*plugin, 0.4, low);
            auto high = noteOn(72);
            renderBlock(*plugin, 0.5, high);

            // Letting the top note go should return to the note underneath it,
            // not cut off - that fallback is what makes legato lines work.
            auto releaseHigh = noteOff(72);
            const float afterRelease = renderBlock(*plugin, 0.6, releaseHigh);
            expectWithinAbsoluteError(afterRelease, expectedLevelForNote(60), 0.002f);
        }

        beginTest("Glide ramps between notes instead of jumping");
        {
            setHostParam(*plugin, voiceModeIdx, 0.5f);
            instrument->reset();

            // 0 ms: the second note lands immediately.
            setHostParam(*plugin, glideIdx, 0.0f);
            auto from = noteOn(60);
            renderBlock(*plugin, 0.7, from);
            auto to = noteOn(72);
            const float instant = renderBlock(*plugin, 0.8, to);
            expectWithinAbsoluteError(instant, expectedLevelForNote(72), 0.002f);

            // 500 ms against a 64-sample block (~1.5 ms): one block in, the
            // pitch must have left the old note without arriving at the new one.
            setHostParam(*plugin, glideIdx, 0.25f);  // 0.25 * 2000 ms
            instrument->reset();
            auto glideFrom = noteOn(60);
            renderBlock(*plugin, 0.9, glideFrom);
            auto glideTo = noteOn(72);
            const float mid = renderBlock(*plugin, 1.0, glideTo);

            const float lowLevel = expectedLevelForNote(60);
            const float highLevel = expectedLevelForNote(72);
            expect(mid > lowLevel, "Glide should have started moving off the first note");
            expect(mid < highLevel * 0.9f,
                   "Glide should not have reached the second note after ~1.5 ms of a 500 ms ramp");
        }

        beginTest("Switching modes silences the engine being left behind");
        {
            // A note held in Mono has no note-off delivered when the mode
            // changes, so without a flush it would sound forever underneath the
            // poly engine with nothing able to release it.
            setHostParam(*plugin, voiceModeIdx, 0.5f);
            setHostParam(*plugin, glideIdx, 0.0f);
            instrument->reset();

            auto held = noteOn(60);
            const float sounding = renderBlock(*plugin, 1.1, held);
            expect(sounding > 0.001f, "Mono note should be sounding before the switch");

            setHostParam(*plugin, voiceModeIdx, 0.0f);  // back to Poly
            te::MidiMessageArray none;
            const float afterSwitch = renderBlock(*plugin, 1.2, none);
            expectWithinAbsoluteError(afterSwitch, 0.0f, 0.001f);
        }

        beginTest("Pitch bend moves the sounding pitch by the Bend Range");
        {
            const int bendIdx = audio::FaustInstrumentPlugin::kBendRangeParamIndex;
            constexpr float kMax = audio::FaustInstrumentPlugin::kMaxBendSemitones;
            // Every assertion below is against the DSP's own freq zone, so this
            // fails if the wheel is forwarded to the allocator (where it lands
            // in an empty midi::pitchWheel) instead of driving freq directly.

            // --- Poly: wheel up on a sounding note -------------------------
            setHostParam(*plugin, voiceModeIdx, 0.0f);
            setHostParam(*plugin, glideIdx, 0.0f);
            setHostParam(*plugin, bendIdx, 2.0f / kMax);
            instrument->reset();

            auto held = noteOn(69);
            const float unbent = renderBlock(*plugin, 1.3, held);
            expectWithinAbsoluteError(unbent, expectedLevelForNote(69), 0.002f);

            auto up = pitchWheel(1.0f);
            const float bentUp = renderBlock(*plugin, 1.4, up);
            expectWithinAbsoluteError(bentUp, expectedLevelForBentNote(69, 2.0f), 0.002f);

            // --- and back down through centre ------------------------------
            auto down = pitchWheel(-1.0f);
            const float bentDown = renderBlock(*plugin, 1.5, down);
            expectWithinAbsoluteError(bentDown, expectedLevelForBentNote(69, -2.0f), 0.002f);

            // --- A note started while the wheel is held is bent too --------
            // keyOn writes an unbent freq, so this covers the re-apply that
            // has to follow it rather than the wheel handler alone.
            auto release = noteOff(69);
            renderBlock(*plugin, 1.6, release);
            auto late = noteOn(69);
            const float startedBent = renderBlock(*plugin, 1.7, late);
            expectWithinAbsoluteError(startedBent, expectedLevelForBentNote(69, -2.0f), 0.002f);

            // --- Range scales the same wheel position ----------------------
            setHostParam(*plugin, bendIdx, 12.0f / kMax);
            auto upAgain = pitchWheel(1.0f);
            const float octave = renderBlock(*plugin, 1.8, upAgain);
            expectWithinAbsoluteError(octave, expectedLevelForBentNote(69, 12.0f), 0.004f);

            // --- Range 0 makes the wheel inert -----------------------------
            setHostParam(*plugin, bendIdx, 0.0f);
            auto stillUp = pitchWheel(1.0f);
            const float inert = renderBlock(*plugin, 1.9, stillUp);
            expectWithinAbsoluteError(inert, expectedLevelForNote(69), 0.002f);

            // --- reset() recentres the wheel -------------------------------
            // Otherwise a transport stop or mode change, neither of which
            // delivers a note-off or a wheel message, would leave everything
            // played afterwards silently detuned.
            setHostParam(*plugin, bendIdx, 2.0f / kMax);
            auto bendUp = pitchWheel(1.0f);
            renderBlock(*plugin, 1.91, bendUp);
            instrument->reset();
            auto afterReset = noteOn(69);
            const float unbentAgain = renderBlock(*plugin, 1.92, afterReset);
            expectWithinAbsoluteError(unbentAgain, expectedLevelForNote(69), 0.002f);
        }

        beginTest("Pitch bend works in Mono, which ignored the wheel entirely");
        {
            const int bendIdx = audio::FaustInstrumentPlugin::kBendRangeParamIndex;
            constexpr float kMax = audio::FaustInstrumentPlugin::kMaxBendSemitones;

            setHostParam(*plugin, voiceModeIdx, 0.5f);  // Mono
            setHostParam(*plugin, glideIdx, 0.0f);
            setHostParam(*plugin, bendIdx, 2.0f / kMax);
            instrument->reset();

            // The previous block left the wheel up; reset() recentres it, so
            // this note starts at concert pitch without a wheel message.
            auto held = noteOn(69);
            const float unbent = renderBlock(*plugin, 2.0, held);
            expectWithinAbsoluteError(unbent, expectedLevelForNote(69), 0.002f);

            auto up = pitchWheel(1.0f);
            const float bent = renderBlock(*plugin, 2.1, up);
            expectWithinAbsoluteError(bent, expectedLevelForBentNote(69, 2.0f), 0.002f);

            // Bend must ride on top of the glide ramp rather than replacing it:
            // with the wheel still held, a new note lands bent as well.
            auto next = noteOn(72);
            const float movedBent = renderBlock(*plugin, 2.2, next);
            expectWithinAbsoluteError(movedBent, expectedLevelForBentNote(72, 2.0f), 0.002f);
        }

        plugin->baseClassDeinitialise();
    }
};

// Reports the DSP instance's own initialisation-time sample rate as its output
// level, which is the only way to see whether a voice was re-initialised for
// the device rate. fconstant is spelled out rather than taken from ma.SR
// because the test binary has no faustlibraries dir to import from.
constexpr const char* kSampleRateDsp = R"FAUST(
// The literal "stdfaust.lib" in this comment is load-bearing; see kVoiceModeDsp.
gate = button("gate");
SR = fconstant(int fSamplingFreq, <math.h>);
process = (SR / 96000.0) * gate <: _, _;
)FAUST";

// The instrument compiles at a provisional 44.1 kHz in its constructor, so
// every voice has to be re-initialised when initialise() learns the real device
// rate. The poly allocator always was; the Mono/Legato voice is a separate
// instance the allocator knows nothing about, and was left behind.
class FaustInstrumentSampleRateTest final : public juce::UnitTest {
  public:
    FaustInstrumentSampleRateTest()
        : juce::UnitTest("Faust Instrument Sample Rate Tests", "magda") {}

    void runTest() override {
        beginTest("Both engines follow the device sample rate, not the provisional one");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr, "Test edit should be created");
        if (!edit)
            return;

        auto plugin = createFaustInstrument(*edit);
        // The device inside the host wrapper (#2315).
        auto* instrument =
            audio::tracktion_adapter::deviceFromPlugin<audio::FaustInstrumentPlugin>(plugin.get());
        expect(instrument != nullptr, "Faust instrument should be created");
        if (instrument == nullptr)
            return;

        juce::String err;
        expect(instrument->loadDspSource("SampleRateTest", kSampleRateDsp, err),
               "Test DSP should compile: " + err);

        // Deliberately not 44100: the provisional rate the constructor compiled
        // at is 44100, so testing at that rate cannot tell the two apart.
        constexpr double kDeviceRate = 48000.0;
        te::PluginInitialisationInfo initInfo;
        initInfo.startTime = tracktion::TimePosition();
        initInfo.sampleRate = kDeviceRate;
        initInfo.blockSizeSamples = kBlockSize;
        plugin->baseClassInitialise(initInfo);

        const float expected = static_cast<float>(kDeviceRate / 96000.0);
        const int voiceModeIdx = audio::FaustInstrumentPlugin::kVoiceModeParamIndex;

        {
            setHostParam(*plugin, voiceModeIdx, 0.0f);  // Poly
            instrument->reset();
            auto note = noteOn(69);
            const float level = renderBlock(*plugin, 0.0, note);
            expectWithinAbsoluteError(level, expected, 0.002f);
        }

        {
            setHostParam(*plugin, voiceModeIdx, 0.5f);  // Mono
            instrument->reset();
            auto note = noteOn(69);
            const float level = renderBlock(*plugin, 0.1, note);
            expectWithinAbsoluteError(level, expected, 0.002f);
        }

        plugin->baseClassDeinitialise();
    }
};

FaustInstrumentVoiceModeTest faustInstrumentVoiceModeTest;
FaustInstrumentSampleRateTest faustInstrumentSampleRateTest;

}  // namespace
