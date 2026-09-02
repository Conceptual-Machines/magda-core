#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <cmath>

#include "SharedTestEngine.hpp"
#include "magda/daw/audio/plugins/MagdaConvolutionPlugin.hpp"
#include "magda/daw/audio/plugins/tracktion/TracktionDeviceStateBridge.hpp"
#include "magda/daw/audio/plugins/tracktion/TracktionMagdaDevicePlugin.hpp"
#include "magda/daw/core/DeviceState.hpp"
#include "magda/daw/core/ParameterUtils.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"

// The native convolution device that replaced te::ImpulseResponsePlugin (#1980).
//
// What matters here is what a project depends on: the impulse response survives
// save and reload, the convolution actually convolves, and the parameter ranges
// normalise exactly as the retired device's did - which is the whole reason its
// automation and macro links are allowed to carry over.
//
// The device is a MagdaDevice (#2299): the chain holds the host's wrapper and
// the parameters are normalised slots, so everything here reaches the device
// through the adapter and converts display values through its ParameterInfo.

namespace {

namespace audio = magda::daw::audio;
namespace ta = magda::daw::audio::tracktion_adapter;
namespace ds = magda::device_state;
namespace te = tracktion::engine;

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 256;
constexpr int kDelaySamples = 8;

audio::MagdaConvolutionPlugin* createConvolution(te::Edit& edit, te::Plugin::Ptr& holder) {
    juce::ValueTree state(te::IDs::PLUGIN);
    state.setProperty(te::IDs::type, audio::MagdaConvolutionPlugin::xmlTypeName, nullptr);
    holder = edit.getPluginCache().createNewPlugin(state);
    return ta::deviceFromPlugin<audio::MagdaConvolutionPlugin>(holder.get());
}

/// Write the slot's DISPLAY value through the wrapper's parameter, which is how
/// every host write reaches a wrapped device.
void setSlotDisplayValue(te::Plugin& plugin, const audio::MagdaConvolutionPlugin& device, int slot,
                         float displayValue) {
    auto* adapter = dynamic_cast<ta::TracktionMagdaDevicePlugin*>(&plugin);
    if (auto* param = adapter != nullptr ? adapter->parameterForDeviceSlot(slot) : nullptr)
        param->setParameterFromHost(
            magda::ParameterUtils::realToNormalized(displayValue, device.parameterInfo(slot)),
            juce::sendNotificationSync);
}

float slotDisplayValue(te::Plugin& plugin, const audio::MagdaConvolutionPlugin& device, int slot) {
    auto* adapter = dynamic_cast<ta::TracktionMagdaDevicePlugin*>(&plugin);
    if (auto* param = adapter != nullptr ? adapter->parameterForDeviceSlot(slot) : nullptr)
        return magda::ParameterUtils::normalizedToReal(param->getCurrentValue(),
                                                       device.parameterInfo(slot));
    return 0.0f;
}

/// A mono IR that is silent except for a single spike `kDelaySamples` in, so a
/// convolved impulse comes back out at a position the test can check exactly.
juce::File writeDelayImpulseResponse() {
    auto file = juce::File::createTempFile(".wav");

    juce::AudioBuffer<float> ir(1, kDelaySamples + 1);
    ir.clear();
    ir.setSample(0, kDelaySamples, 1.0f);

    juce::WavAudioFormat format;
    auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
    if (auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions()
                                                         .withSampleRate(kSampleRate)
                                                         .withNumChannels(1)
                                                         .withBitsPerSample(24)))
        writer->writeFromAudioSampleBuffer(ir, 0, ir.getNumSamples());

    return file;
}

/// Render one block of a unit impulse through the plugin and hand back the
/// left channel. The plugin has to be initialised first.
juce::AudioBuffer<float> renderImpulse(te::Plugin& plugin) {
    juce::AudioBuffer<float> buffer(2, kBlockSize);
    buffer.clear();
    buffer.setSample(0, 0, 1.0f);
    buffer.setSample(1, 0, 1.0f);

    te::MidiMessageArray midi;
    te::PluginRenderContext context(&buffer, juce::AudioChannelSet::stereo(), 0, kBlockSize, &midi,
                                    0.0, tracktion::TimeRange(), true, false, false, true);
    plugin.applyToBuffer(context);
    return buffer;
}

int peakIndex(const juce::AudioBuffer<float>& buffer) {
    int index = 0;
    float peak = 0.0f;
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        const auto magnitude = std::abs(buffer.getSample(0, i));
        if (magnitude > peak) {
            peak = magnitude;
            index = i;
        }
    }
    return index;
}

void prepareForRender(te::Plugin& plugin) {
    te::PluginInitialisationInfo info;
    info.startTime = tracktion::TimePosition();
    info.sampleRate = kSampleRate;
    info.blockSizeSamples = kBlockSize;
    plugin.baseClassInitialise(info);
}

class ConvolutionDeviceTest final : public juce::UnitTest {
  public:
    ConvolutionDeviceTest() : juce::UnitTest("Convolution Device", "magda") {}

    void runTest() override {
        beginTest("Engine setup");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr);
        if (edit == nullptr)
            return;

        testParameterRangesMatchTheRetiredDevice(*edit);
        testLoadedImpulseResponseConvolves(*edit);
        testTrimSilenceReachesTheDsp(*edit);
        testImpulseResponseSurvivesSaveAndReload(*edit);
        testRestoringAStatelessDocumentUnloadsTheImpulseResponse(*edit);
        testDryPassesThroughUntouched(*edit);
    }

  private:
    void testParameterRangesMatchTheRetiredDevice(te::Edit& edit) {
        beginTest("Parameter ranges normalise exactly as the retired device's did");

        te::Plugin::Ptr holder;
        auto* convolution = createConvolution(edit, holder);
        expect(convolution != nullptr);
        if (convolution == nullptr)
            return;

        expectEquals(holder->getAutomatableParameters().size(),
                     static_cast<int>(audio::MagdaConvolutionPlugin::kNumParams));

        // The retired device stored a MIDI note number over a linear range from
        // 10 Hz to 20 kHz. The native one stores Hz, but a normalised position
        // still has to land on the same frequency, or every saved automation
        // curve and macro link would shift when the project migrated.
        const auto noteToFrequency = [](float note) {
            return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
        };
        const float noteMin = 69.0f + 12.0f * std::log2(10.0f / 440.0f);
        const float noteMax = 69.0f + 12.0f * std::log2(20000.0f / 440.0f);

        const auto lowCutInfo = convolution->parameterInfo(audio::MagdaConvolutionPlugin::kLowCut);
        for (const float normalised : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            const auto retired = noteToFrequency(noteMin + normalised * (noteMax - noteMin));
            expectWithinAbsoluteError(
                magda::ParameterUtils::normalizedToReal(normalised, lowCutInfo), retired,
                retired * 0.001f);
        }

        expectWithinAbsoluteError(magda::ParameterUtils::realToNormalized(110.0f, lowCutInfo),
                                  (45.0f - noteMin) / (noteMax - noteMin), 0.001f);

        // Gain, mix and Q keep the retired device's real ranges outright.
        const auto gainInfo = convolution->parameterInfo(audio::MagdaConvolutionPlugin::kGain);
        expectWithinAbsoluteError(gainInfo.minValue, -12.0f, 0.0001f);
        expectWithinAbsoluteError(gainInfo.maxValue, 6.0f, 0.0001f);
        // setSkewForCentre(0): unity gain sits at the middle of the knob.
        expectWithinAbsoluteError(magda::ParameterUtils::normalizedToReal(0.5f, gainInfo), 0.0f,
                                  0.01f);

        const auto qInfo = convolution->parameterInfo(audio::MagdaConvolutionPlugin::kFilterQ);
        expectWithinAbsoluteError(qInfo.minValue, 0.1f, 0.0001f);
        expectWithinAbsoluteError(qInfo.maxValue, 14.0f, 0.0001f);

        holder->deleteFromParent();
    }

    void testLoadedImpulseResponseConvolves(te::Edit& edit) {
        beginTest("A loaded impulse response convolves the signal");

        auto irFile = writeDelayImpulseResponse();

        te::Plugin::Ptr holder;
        auto* convolution = createConvolution(edit, holder);
        expect(convolution != nullptr);
        if (convolution == nullptr)
            return;

        expect(convolution->loadImpulseResponse(irFile), "the IR file failed to load");

        holder->flushPluginStateToValueTree();
        expect(holder->state.getProperty(te::IDs::irFileData).getBinaryData() != nullptr,
               "the IR was not flushed into the plugin state");

        setSlotDisplayValue(*holder, *convolution, audio::MagdaConvolutionPlugin::kMix, 1.0f);
        prepareForRender(*holder);

        // The IR spikes kDelaySamples in, so the impulse comes back out there.
        const auto rendered = renderImpulse(*holder);
        expectEquals(peakIndex(rendered), kDelaySamples, "the IR did not delay the impulse");
        expectGreaterThan(rendered.getMagnitude(0, kBlockSize), 0.1f);

        expectWithinAbsoluteError(holder->getLatencySeconds(), 0.0, 1.0e-9);
        expectGreaterThan(holder->getTailLength(), 0.0);

        // A control still moving puts the render on its sub-blocked path, where
        // the filters and the trim are re-derived every 32 samples. The
        // convolution has to come out of it unchanged, only louder.
        setSlotDisplayValue(*holder, *convolution, audio::MagdaConvolutionPlugin::kGain, 6.0f);
        const auto smoothed = renderImpulse(*holder);
        expectEquals(peakIndex(smoothed), kDelaySamples,
                     "the sub-blocked control path broke the convolution");
        expectGreaterThan(smoothed.getMagnitude(0, kBlockSize),
                          rendered.getMagnitude(0, kBlockSize));

        holder->deleteFromParent();
        irFile.deleteFile();
    }

    void testTrimSilenceReachesTheDsp(te::Edit& edit) {
        beginTest("Trim silence is not a parameter but still reaches the convolution");

        auto irFile = writeDelayImpulseResponse();

        te::Plugin::Ptr holder;
        auto* convolution = createConvolution(edit, holder);
        expect(convolution != nullptr);
        if (convolution == nullptr)
            return;

        // Set before the IR is loaded: the flag is read when the blob is
        // decoded, and prepare() then flushes that pending load.
        convolution->setTrimSilence(true);
        expect(convolution->loadImpulseResponse(irFile));
        setSlotDisplayValue(*holder, *convolution, audio::MagdaConvolutionPlugin::kMix, 1.0f);
        prepareForRender(*holder);

        // With the leading silence gone, the same IR stops delaying anything.
        expectEquals(peakIndex(renderImpulse(*holder)), 0,
                     "trimSilence did not reach the convolution");

        holder->deleteFromParent();
        irFile.deleteFile();
    }

    void testImpulseResponseSurvivesSaveAndReload(te::Edit& edit) {
        beginTest("The impulse response survives capture and restore");

        auto irFile = writeDelayImpulseResponse();

        te::Plugin::Ptr holder;
        auto* convolution = createConvolution(edit, holder);
        expect(convolution != nullptr);
        if (convolution == nullptr)
            return;

        expect(convolution->loadImpulseResponse(irFile));
        convolution->setIrName("Test Space");
        convolution->setNormalise(false);
        setSlotDisplayValue(*holder, *convolution, audio::MagdaConvolutionPlugin::kLowCut, 180.0f);

        const auto captured = ta::captureInternalDeviceState(*holder, {});
        holder->deleteFromParent();
        irFile.deleteFile();

        const auto doc = ds::decode(captured);
        expect(doc.has_value(), "the captured state is not a MAGDA document");
        if (!doc)
            return;
        expectEquals(doc->deviceType, juce::String(audio::MagdaConvolutionPlugin::xmlTypeName));
        expect(doc->root.props["irFileData"].getBinaryData() != nullptr,
               "the captured state lost the impulse response");

        // #2317: the document is authored state only; the low-cut travels in
        // DeviceInfo::parameters, not here.
        expect(doc->params.empty(), "capture wrote a duplicate parameter record");

        // What loading a project does: rebuild the plugin from the document.
        auto tree = ta::devicePluginTreeFromState(captured);
        expect(tree.isValid());
        auto restoredHolder = edit.getPluginCache().createNewPlugin(tree);

        auto* restored = ta::deviceFromPlugin<audio::MagdaConvolutionPlugin>(restoredHolder.get());
        expect(restored != nullptr);
        if (restored == nullptr)
            return;

        expectEquals(restored->irName(), juce::String("Test Space"));
        expect(!restored->normalise(), "the normalise flag did not round-trip");
        expect(!restored->trimSilence());
        // The parameter restores from the model array, the way a project load
        // seats it (syncFromDeviceInfo), not from the document.
        setSlotDisplayValue(*restoredHolder, *restored, audio::MagdaConvolutionPlugin::kLowCut,
                            180.0f);
        expectWithinAbsoluteError(
            slotDisplayValue(*restoredHolder, *restored, audio::MagdaConvolutionPlugin::kLowCut),
            180.0f, 0.5f);

        setSlotDisplayValue(*restoredHolder, *restored, audio::MagdaConvolutionPlugin::kMix, 1.0f);
        prepareForRender(*restoredHolder);

        const auto rendered = renderImpulse(*restoredHolder);
        expectEquals(peakIndex(rendered), kDelaySamples,
                     "the reloaded device is not convolving with the saved IR");

        restoredHolder->deleteFromParent();
    }

    // Undoing the FIRST IR load restores a document with no `irFileData`.
    // Absence means none (#2317 review): the device must unload - stop
    // convolving, drop the name, and stop writing the blob back on the next
    // flush, or capture would resurrect the "undone" IR into the model.
    void testRestoringAStatelessDocumentUnloadsTheImpulseResponse(te::Edit& edit) {
        beginTest("Restoring a document without an IR unloads the device");

        auto irFile = writeDelayImpulseResponse();

        te::Plugin::Ptr holder;
        auto* convolution = createConvolution(edit, holder);
        expect(convolution != nullptr);
        if (convolution == nullptr)
            return;

        expect(convolution->loadImpulseResponse(irFile));
        convolution->setIrName("Doomed");
        irFile.deleteFile();
        expect(convolution->irName() == "Doomed");

        // What setDeviceAuthoredState projects for an empty snapshot: a bare
        // typed tree, nothing authored.
        juce::ValueTree bare(te::IDs::PLUGIN);
        bare.setProperty(te::IDs::type, audio::MagdaConvolutionPlugin::xmlTypeName, nullptr);
        convolution->restoreState(bare);

        expect(convolution->irName().isEmpty(), "the un-loaded IR kept its name");
        expectWithinAbsoluteError(convolution->properties().tailLengthSeconds, 0.0, 1.0e-9,
                                  "the un-loaded IR still reports a tail");

        // A capture after the unload must not resurrect the blob.
        juce::ValueTree flushed(te::IDs::PLUGIN);
        convolution->flushState(flushed);
        expect(!flushed.hasProperty(te::IDs::irFileData),
               "flush wrote an impulse response the device no longer holds");

        // And the unloaded device passes signal instead of convolving.
        setSlotDisplayValue(*holder, *convolution, audio::MagdaConvolutionPlugin::kMix, 1.0f);
        prepareForRender(*holder);
        const auto rendered = renderImpulse(*holder);
        expectEquals(peakIndex(rendered), 0,
                     "the un-loaded device is still convolving with the old IR");

        holder->deleteFromParent();
    }

    void testDryPassesThroughUntouched(te::Edit& edit) {
        beginTest("A fully dry device passes the signal through");

        te::Plugin::Ptr holder;
        auto* convolution = createConvolution(edit, holder);
        expect(convolution != nullptr);
        if (convolution == nullptr)
            return;

        setSlotDisplayValue(*holder, *convolution, audio::MagdaConvolutionPlugin::kMix, 0.0f);
        prepareForRender(*holder);

        const auto rendered = renderImpulse(*holder);
        expectWithinAbsoluteError(rendered.getSample(0, 0), 1.0f, 0.001f);
        expectEquals(peakIndex(rendered), 0);

        holder->deleteFromParent();
    }
};

ConvolutionDeviceTest convolutionDeviceTest;

}  // namespace
