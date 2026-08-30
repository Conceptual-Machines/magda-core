#include <juce_audio_basics/juce_audio_basics.h>
#include <tracktion_engine/tracktion_engine.h>

#include <cmath>

#include "SharedTestEngine.hpp"
#include "magda/daw/audio/plugins/FaustInstrumentPlugin.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"

/**
 * The patch every Faust Instrument comes up with, played (#2237).
 *
 * The other Faust tests in this binary compile a self-contained DSP written for
 * them, which is right for what they ask: whether a voice is allocated, whether
 * a glide ramps, whether a control binds. Nothing was playing the patch a user
 * actually gets, and the patch a user actually gets divided by zero.
 *
 * `fi.resonlp`'s second argument is a Q and it computes `1/Q`. The patch handed
 * it a control declared from zero, so at the bottom of that slider the filter's
 * feedback coefficient came out `(a0 - inf + csq)/inf`, which is NaN, and the
 * voice output NaN from its first sample onward and put it on the master.
 *
 * Asserted as "finite everywhere across the control's travel" rather than at the
 * one value that failed. The bottom of the range is where the divide by zero
 * was; a bound that only checked there would miss the next patch that goes
 * non-finite somewhere in the middle, and a filter is exactly the place that
 * happens.
 */

namespace {

namespace audio = magda::daw::audio;
namespace te = tracktion::engine;

constexpr int kBlockSize = 64;
constexpr double kSampleRate = 44100.0;

/// The pool slot each control declares through its `[idx:N]` metadata. The
/// patch names them, so a test that hard-codes a position rather than the idx
/// would silently follow a reordering it should have failed on.
constexpr int kCutoffSlot = 0;
constexpr int kResonanceSlot = 1;

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

te::MidiMessageArray noteOn(int note) {
    te::MidiMessageArray midi;
    midi.addMidiMessage(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(127)), 0.0,
                        te::MidiMessageArray::notMPE);
    return midi;
}

/// Renders one block and returns the largest magnitude in it, or a non-finite
/// value if anything in the block was.
///
/// The peak rather than one sample: a filter that has gone non-finite may still
/// hand back a finite sample at the position a spot check happens to read, and
/// what is being asked is whether the block is playable at all.
float renderPeak(audio::FaustInstrumentPlugin& instrument, double startSeconds,
                 te::MidiMessageArray& midi) {
    juce::AudioBuffer<float> buffer(2, kBlockSize);
    buffer.clear();

    te::PluginRenderContext context(
        &buffer, juce::AudioChannelSet::stereo(), 0, kBlockSize, &midi, 0.0,
        tracktion::TimeRange(
            tracktion::TimePosition::fromSeconds(startSeconds),
            tracktion::TimePosition::fromSeconds(startSeconds + (kBlockSize / kSampleRate))),
        true, false, false, false);
    instrument.applyToBuffer(context);

    float peak = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < kBlockSize; ++sample) {
            const auto value = buffer.getSample(channel, sample);
            if (!std::isfinite(value))
                return value;
            peak = std::max(peak, std::abs(value));
        }

    return peak;
}

class FaustInstrumentDefaultPatchTest final : public juce::UnitTest {
  public:
    FaustInstrumentDefaultPatchTest() : juce::UnitTest("Faust Instrument Default Patch", "magda") {}

    void runTest() override {
        // Before anything is expected, because JUCE's runner records a result
        // into the test that beginTest opened and dereferences it unguarded: an
        // expect() ahead of the first one asserts and then reads a null.
        beginTest("The default patch compiles and binds its own controls");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr);
        if (edit == nullptr)
            return;

        auto plugin = createFaustInstrument(*edit);
        auto* instrument = dynamic_cast<audio::FaustInstrumentPlugin*>(plugin.get());
        expect(instrument != nullptr, "the default Faust instrument should instantiate");
        if (instrument == nullptr)
            return;

        te::PluginInitialisationInfo initInfo;
        initInfo.startTime = tracktion::TimePosition();
        initInfo.sampleRate = kSampleRate;
        initInfo.blockSizeSamples = kBlockSize;
        instrument->baseClassInitialise(initInfo);

        // If the standard library is not beside the binary the patch will not
        // compile and the device falls back, which would leave everything below
        // asserting about a passthrough. Said out loud rather than discovered as
        // a silent pass, because that is the shape this whole area keeps taking.
        expect(instrument->getAutomatableParameters().size() > kResonanceSlot,
               "the default patch bound no controls, so its source did not compile");

        beginTest("It plays a finite signal across the whole resonance travel");

        // Every step of the control, not the ends. The divide by zero lived at
        // the bottom, and a filter is the kind of thing that can go non-finite
        // anywhere along a coefficient curve rather than only where somebody
        // predicted.
        constexpr int kSteps = 21;

        for (int step = 0; step < kSteps; ++step) {
            const auto resonance = static_cast<float>(step) / static_cast<float>(kSteps - 1);

            setHostParam(*instrument, kResonanceSlot, resonance);

            // Mid-travel, so the filter is passing the oscillator rather than
            // sitting above or below everything it plays.
            setHostParam(*instrument, kCutoffSlot, 0.5f);

            auto on = noteOn(60);
            auto sustain = te::MidiMessageArray();

            auto peak = renderPeak(*instrument, static_cast<double>(step), on);
            for (int block = 0; block < 8; ++block)
                peak = std::max(peak, renderPeak(*instrument,
                                                 static_cast<double>(step) +
                                                     ((block + 1) * kBlockSize) / kSampleRate,
                                                 sustain));

            expect(std::isfinite(peak),
                   "resonance " + juce::String(resonance, 2) + " rendered a non-finite sample");

            // And it is a filter rather than a mute. A resonance control that
            // silenced the voice at one end would satisfy the finiteness check
            // above perfectly well.
            expect(peak > 0.0f, "resonance " + juce::String(resonance, 2) + " rendered silence");
        }
    }
};

FaustInstrumentDefaultPatchTest faustInstrumentDefaultPatchTest;

}  // namespace
