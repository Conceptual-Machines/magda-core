#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <cmath>

#include "SharedTestEngine.hpp"
#include "magda/daw/audio/plugins/FaustInstrumentPlugin.hpp"
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

void setHostParam(audio::FaustInstrumentPlugin& instrument, int paramIndex, float normalised) {
    auto params = instrument.getAutomatableParameters();
    if (paramIndex < params.size() && params[paramIndex])
        params[paramIndex]->setParameterFromHost(normalised, juce::sendNotificationSync);
}

// Renders one block and returns its final sample. Taking the last sample rather
// than the first matters for the glide tests: the ramp advances across the
// block, so the end is where movement has actually happened.
float renderBlock(audio::FaustInstrumentPlugin& instrument, double startSeconds,
                  te::MidiMessageArray& midi) {
    juce::AudioBuffer<float> buffer(2, kBlockSize);
    buffer.clear();

    te::PluginRenderContext context(
        &buffer, juce::AudioChannelSet::stereo(), 0, kBlockSize, &midi, 0.0,
        tracktion::TimeRange(
            tracktion::TimePosition::fromSeconds(startSeconds),
            tracktion::TimePosition::fromSeconds(startSeconds + kBlockSize / kSampleRate)),
        true, false, false, false);
    instrument.applyToBuffer(context);
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
        auto* instrument = dynamic_cast<audio::FaustInstrumentPlugin*>(plugin.get());
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
        instrument->baseClassInitialise(initInfo);

        const int voiceModeIdx = audio::FaustInstrumentPlugin::kVoiceModeParamIndex;
        const int glideIdx = audio::FaustInstrumentPlugin::kGlideParamIndex;

        {
            auto params = instrument->getAutomatableParameters();
            expect(params.size() > audio::FaustInstrumentPlugin::kBendRangeParamIndex,
                   "Instrument should expose three parameters beyond the 64-slot pool");
        }

        beginTest("Poly stacks voices; Mono collapses them to one");
        {
            // Poly is the default. Two held notes should sound together, so the
            // level is the sum of both - the point of the mode.
            setHostParam(*instrument, voiceModeIdx, 0.0f);
            setHostParam(*instrument, glideIdx, 0.0f);
            instrument->reset();

            auto first = noteOn(60);
            renderBlock(*instrument, 0.0, first);
            auto second = noteOn(72);
            const float polyLevel = renderBlock(*instrument, 0.1, second);

            const float bothNotes = expectedLevelForNote(60) + expectedLevelForNote(72);
            expectWithinAbsoluteError(polyLevel, bothNotes, 0.002f);

            // Mono: the same two notes leave only the newest sounding.
            setHostParam(*instrument, voiceModeIdx, 0.5f);  // 0.5 * 2 == Mono
            instrument->reset();
            auto monoFirst = noteOn(60);
            renderBlock(*instrument, 0.2, monoFirst);
            auto monoSecond = noteOn(72);
            const float monoLevel = renderBlock(*instrument, 0.3, monoSecond);

            expectWithinAbsoluteError(monoLevel, expectedLevelForNote(72), 0.002f);
            expect(monoLevel < bothNotes * 0.75f, "Mono should not stack the two notes");
        }

        beginTest("Releasing a Mono note falls back to the one still held");
        {
            setHostParam(*instrument, voiceModeIdx, 0.5f);
            setHostParam(*instrument, glideIdx, 0.0f);
            instrument->reset();

            auto low = noteOn(60);
            renderBlock(*instrument, 0.4, low);
            auto high = noteOn(72);
            renderBlock(*instrument, 0.5, high);

            // Letting the top note go should return to the note underneath it,
            // not cut off - that fallback is what makes legato lines work.
            auto releaseHigh = noteOff(72);
            const float afterRelease = renderBlock(*instrument, 0.6, releaseHigh);
            expectWithinAbsoluteError(afterRelease, expectedLevelForNote(60), 0.002f);
        }

        beginTest("Glide ramps between notes instead of jumping");
        {
            setHostParam(*instrument, voiceModeIdx, 0.5f);
            instrument->reset();

            // 0 ms: the second note lands immediately.
            setHostParam(*instrument, glideIdx, 0.0f);
            auto from = noteOn(60);
            renderBlock(*instrument, 0.7, from);
            auto to = noteOn(72);
            const float instant = renderBlock(*instrument, 0.8, to);
            expectWithinAbsoluteError(instant, expectedLevelForNote(72), 0.002f);

            // 500 ms against a 64-sample block (~1.5 ms): one block in, the
            // pitch must have left the old note without arriving at the new one.
            setHostParam(*instrument, glideIdx, 0.25f);  // 0.25 * 2000 ms
            instrument->reset();
            auto glideFrom = noteOn(60);
            renderBlock(*instrument, 0.9, glideFrom);
            auto glideTo = noteOn(72);
            const float mid = renderBlock(*instrument, 1.0, glideTo);

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
            setHostParam(*instrument, voiceModeIdx, 0.5f);
            setHostParam(*instrument, glideIdx, 0.0f);
            instrument->reset();

            auto held = noteOn(60);
            const float sounding = renderBlock(*instrument, 1.1, held);
            expect(sounding > 0.001f, "Mono note should be sounding before the switch");

            setHostParam(*instrument, voiceModeIdx, 0.0f);  // back to Poly
            te::MidiMessageArray none;
            const float afterSwitch = renderBlock(*instrument, 1.2, none);
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
            setHostParam(*instrument, voiceModeIdx, 0.0f);
            setHostParam(*instrument, glideIdx, 0.0f);
            setHostParam(*instrument, bendIdx, 2.0f / kMax);
            instrument->reset();

            auto held = noteOn(69);
            const float unbent = renderBlock(*instrument, 1.3, held);
            expectWithinAbsoluteError(unbent, expectedLevelForNote(69), 0.002f);

            auto up = pitchWheel(1.0f);
            const float bentUp = renderBlock(*instrument, 1.4, up);
            expectWithinAbsoluteError(bentUp, expectedLevelForBentNote(69, 2.0f), 0.002f);

            // --- and back down through centre ------------------------------
            auto down = pitchWheel(-1.0f);
            const float bentDown = renderBlock(*instrument, 1.5, down);
            expectWithinAbsoluteError(bentDown, expectedLevelForBentNote(69, -2.0f), 0.002f);

            // --- A note started while the wheel is held is bent too --------
            // keyOn writes an unbent freq, so this covers the re-apply that
            // has to follow it rather than the wheel handler alone.
            auto release = noteOff(69);
            renderBlock(*instrument, 1.6, release);
            auto late = noteOn(69);
            const float startedBent = renderBlock(*instrument, 1.7, late);
            expectWithinAbsoluteError(startedBent, expectedLevelForBentNote(69, -2.0f), 0.002f);

            // --- Range scales the same wheel position ----------------------
            setHostParam(*instrument, bendIdx, 12.0f / kMax);
            auto upAgain = pitchWheel(1.0f);
            const float octave = renderBlock(*instrument, 1.8, upAgain);
            expectWithinAbsoluteError(octave, expectedLevelForBentNote(69, 12.0f), 0.004f);

            // --- Range 0 makes the wheel inert -----------------------------
            setHostParam(*instrument, bendIdx, 0.0f);
            auto stillUp = pitchWheel(1.0f);
            const float inert = renderBlock(*instrument, 1.9, stillUp);
            expectWithinAbsoluteError(inert, expectedLevelForNote(69), 0.002f);

            // --- reset() recentres the wheel -------------------------------
            // Otherwise a transport stop or mode change, neither of which
            // delivers a note-off or a wheel message, would leave everything
            // played afterwards silently detuned.
            setHostParam(*instrument, bendIdx, 2.0f / kMax);
            auto bendUp = pitchWheel(1.0f);
            renderBlock(*instrument, 1.91, bendUp);
            instrument->reset();
            auto afterReset = noteOn(69);
            const float unbentAgain = renderBlock(*instrument, 1.92, afterReset);
            expectWithinAbsoluteError(unbentAgain, expectedLevelForNote(69), 0.002f);
        }

        beginTest("Pitch bend works in Mono, which ignored the wheel entirely");
        {
            const int bendIdx = audio::FaustInstrumentPlugin::kBendRangeParamIndex;
            constexpr float kMax = audio::FaustInstrumentPlugin::kMaxBendSemitones;

            setHostParam(*instrument, voiceModeIdx, 0.5f);  // Mono
            setHostParam(*instrument, glideIdx, 0.0f);
            setHostParam(*instrument, bendIdx, 2.0f / kMax);
            instrument->reset();

            // The previous block left the wheel up; reset() recentres it, so
            // this note starts at concert pitch without a wheel message.
            auto held = noteOn(69);
            const float unbent = renderBlock(*instrument, 2.0, held);
            expectWithinAbsoluteError(unbent, expectedLevelForNote(69), 0.002f);

            auto up = pitchWheel(1.0f);
            const float bent = renderBlock(*instrument, 2.1, up);
            expectWithinAbsoluteError(bent, expectedLevelForBentNote(69, 2.0f), 0.002f);

            // Bend must ride on top of the glide ramp rather than replacing it:
            // with the wheel still held, a new note lands bent as well.
            auto next = noteOn(72);
            const float movedBent = renderBlock(*instrument, 2.2, next);
            expectWithinAbsoluteError(movedBent, expectedLevelForBentNote(72, 2.0f), 0.002f);
        }

        instrument->baseClassDeinitialise();
    }
};

FaustInstrumentVoiceModeTest faustInstrumentVoiceModeTest;

}  // namespace
