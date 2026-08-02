#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <cmath>

#include "SharedTestEngine.hpp"
#include "magda/daw/audio/plugins/MagdaConvolutionPlugin.hpp"
#include "magda/daw/audio/plugins/tracktion/TracktionDeviceStateBridge.hpp"
#include "magda/daw/core/DeviceState.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"

// The native convolution device that replaced te::ImpulseResponsePlugin (#1980).
//
// What matters here is what a project depends on: the impulse response survives
// save and reload, the convolution actually convolves, and the parameter ranges
// normalise exactly as the retired device's did - which is the whole reason its
// automation and macro links are allowed to carry over.

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
    return dynamic_cast<audio::MagdaConvolutionPlugin*>(holder.get());
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

        const auto& params = convolution->getAutomatableParameters();
        expectEquals(params.size(), static_cast<int>(audio::MagdaConvolutionPlugin::kNumParams));

        // The retired device stored a MIDI note number over a linear range from
        // 10 Hz to 20 kHz. The native one stores Hz, but a normalised position
        // still has to land on the same frequency, or every saved automation
        // curve and macro link would shift when the project migrated.
        const auto noteToFrequency = [](float note) {
            return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
        };
        const float noteMin = 69.0f + 12.0f * std::log2(10.0f / 440.0f);
        const float noteMax = 69.0f + 12.0f * std::log2(20000.0f / 440.0f);

        const auto& range = convolution->lowCutParam->valueRange;
        for (const float normalised : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            const auto retired = noteToFrequency(noteMin + normalised * (noteMax - noteMin));
            expectWithinAbsoluteError(range.convertFrom0to1(normalised), retired, retired * 0.001f);
        }

        expectWithinAbsoluteError(range.convertTo0to1(110.0f),
                                  (45.0f - noteMin) / (noteMax - noteMin), 0.001f);

        // Gain, mix and Q keep the retired device's real ranges outright.
        expectWithinAbsoluteError(convolution->gainParam->getValueRange().getStart(), -12.0f,
                                  0.0001f);
        expectWithinAbsoluteError(convolution->gainParam->getValueRange().getEnd(), 6.0f, 0.0001f);
        expectWithinAbsoluteError(convolution->filterQParam->getValueRange().getStart(), 0.1f,
                                  0.0001f);
        expectWithinAbsoluteError(convolution->filterQParam->getValueRange().getEnd(), 14.0f,
                                  0.0001f);

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
        expect(convolution->state.getProperty(te::IDs::irFileData).getBinaryData() != nullptr,
               "the IR was not stored in the device state");

        convolution->mixParam->setParameterFromHost(1.0f, juce::sendNotificationSync);
        prepareForRender(*convolution);

        // The IR spikes kDelaySamples in, so the impulse comes back out there.
        const auto rendered = renderImpulse(*convolution);
        expectEquals(peakIndex(rendered), kDelaySamples, "the IR did not delay the impulse");
        expectGreaterThan(rendered.getMagnitude(0, kBlockSize), 0.1f);

        expectWithinAbsoluteError(convolution->getLatencySeconds(), 0.0, 1.0e-9);
        expectGreaterThan(convolution->getTailLength(), 0.0);

        // A control still moving puts the render on its sub-blocked path, where
        // the filters and the trim are re-derived every 32 samples. The
        // convolution has to come out of it unchanged, only louder.
        convolution->gainParam->setParameterFromHost(6.0f, juce::sendNotificationSync);
        const auto smoothed = renderImpulse(*convolution);
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
        convolution->trimSilence = true;
        expect(convolution->loadImpulseResponse(irFile));
        convolution->mixParam->setParameterFromHost(1.0f, juce::sendNotificationSync);
        prepareForRender(*convolution);

        // With the leading silence gone, the same IR stops delaying anything.
        expectEquals(peakIndex(renderImpulse(*convolution)), 0,
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
        convolution->irName = "Test Space";
        convolution->normalise = false;
        convolution->lowCutParam->setParameterFromHost(180.0f, juce::sendNotificationSync);

        const auto captured = ta::captureInternalDeviceState(*convolution, {});
        holder->deleteFromParent();
        irFile.deleteFile();

        const auto doc = ds::decode(captured);
        expect(doc.has_value(), "the captured state is not a MAGDA document");
        if (!doc)
            return;
        expectEquals(doc->deviceType, juce::String(audio::MagdaConvolutionPlugin::xmlTypeName));
        expect(doc->root.props["irFileData"].getBinaryData() != nullptr,
               "the captured state lost the impulse response");

        // What loading a project does: rebuild the plugin from the document.
        auto tree = ta::devicePluginTreeFromState(captured);
        expect(tree.isValid());
        auto restoredHolder = edit.getPluginCache().createNewPlugin(tree);
        ta::applyDeviceStateParameters(*restoredHolder, captured);

        auto* restored = dynamic_cast<audio::MagdaConvolutionPlugin*>(restoredHolder.get());
        expect(restored != nullptr);
        if (restored == nullptr)
            return;

        expectEquals(restored->irName.get(), juce::String("Test Space"));
        expect(!restored->normalise.get(), "the normalise flag did not round-trip");
        expect(!restored->trimSilence.get());
        expectWithinAbsoluteError(restored->lowCutParam->getCurrentValue(), 180.0f, 0.5f);

        restored->mixParam->setParameterFromHost(1.0f, juce::sendNotificationSync);
        prepareForRender(*restored);

        const auto rendered = renderImpulse(*restored);
        expectEquals(peakIndex(rendered), kDelaySamples,
                     "the reloaded device is not convolving with the saved IR");

        restoredHolder->deleteFromParent();
    }

    void testDryPassesThroughUntouched(te::Edit& edit) {
        beginTest("A fully dry device passes the signal through");

        te::Plugin::Ptr holder;
        auto* convolution = createConvolution(edit, holder);
        expect(convolution != nullptr);
        if (convolution == nullptr)
            return;

        convolution->mixParam->setParameterFromHost(0.0f, juce::sendNotificationSync);
        prepareForRender(*convolution);

        const auto rendered = renderImpulse(*convolution);
        expectWithinAbsoluteError(rendered.getSample(0, 0), 1.0f, 0.001f);
        expectEquals(peakIndex(rendered), 0);

        holder->deleteFromParent();
    }
};

ConvolutionDeviceTest convolutionDeviceTest;

}  // namespace
