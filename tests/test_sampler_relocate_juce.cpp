// Relocating a sampler's file must not re-interpret the sample (#2170).

#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include "JuceTestStateGuard.hpp"
#include "SharedTestEngine.hpp"
#include "magda/daw/audio/plugins/MagdaSamplerPlugin.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"

namespace te = tracktion;
using magda::daw::audio::MagdaSamplerPlugin;

namespace {

te::Plugin::Ptr createCustomPlugin(te::Edit& edit, const juce::String& type) {
    juce::ValueTree state(te::IDs::PLUGIN);
    state.setProperty(te::IDs::type, type, nullptr);
    return edit.getPluginCache().createNewPlugin(state);
}

juce::File testScratchDir() {
    auto dir =
        juce::File(juce::SystemStats::getEnvironmentVariable(
                       "TMPDIR",
                       juce::File::getSpecialLocation(juce::File::tempDirectory).getFullPathName()))
            .getChildFile("magda_juce_tests");
    dir.createDirectory();
    return dir;
}

// Mirrors how the plugin itself moves a marker: through the host-write path,
// with the CachedValue kept in step, since that is what relocateSample reads.
void setMarker(const te::AutomatableParameter::Ptr& param, juce::CachedValue<float>& cached,
               float seconds) {
    param->setParameterFromHost(seconds, juce::dontSendNotification);
    cached = seconds;
}

// One second of sine, long enough that trim markers have somewhere to sit other
// than the defaults.
bool writeTestWav(const juce::File& destination) {
    constexpr double sampleRate = 44100.0;
    constexpr int numSamples = 44100;
    juce::AudioBuffer<float> buffer(1, numSamples);
    const auto phaseInc =
        static_cast<float>(440.0 * juce::MathConstants<double>::twoPi / sampleRate);
    float phase = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        buffer.setSample(0, i, 0.5f * std::sin(phase));
        phase += phaseInc;
    }

    destination.deleteFile();
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(
        new juce::FileOutputStream(destination), sampleRate, 1, 16, {}, 0));
    if (writer == nullptr)
        return false;
    return writer->writeFromAudioSampleBuffer(buffer, 0, numSamples);
}

}  // namespace

class SamplerRelocateTest final : public juce::UnitTest {
  public:
    SamplerRelocateTest() : juce::UnitTest("Sampler Relocate Tests", "magda") {}

    void runTest() override {
        testRelocatePreservesInterpretation();
        testLoadSampleStillResets();
    }

  private:
    // A sampler holding `sampleFile` with deliberately non-default
    // interpretation settings, or nullptr if the fixture could not be built.
    MagdaSamplerPlugin* prepare(te::Edit& edit, const juce::File& sampleFile,
                                te::Plugin::Ptr& keepAlive) {
        keepAlive = createCustomPlugin(edit, MagdaSamplerPlugin::xmlTypeName);
        auto* sampler = dynamic_cast<MagdaSamplerPlugin*>(keepAlive.get());
        expect(sampler != nullptr, "Sampler plugin must be created");
        if (sampler == nullptr)
            return nullptr;

        sampler->loadSample(sampleFile);
        expectEquals(sampler->getSampleFile().getFullPathName(), sampleFile.getFullPathName(),
                     "Sampler must have adopted the sample");

        sampler->setRootNote(48);
        setMarker(sampler->sampleStartParam, sampler->sampleStartValue, 0.25f);
        setMarker(sampler->sampleEndParam, sampler->sampleEndValue, 0.75f);
        setMarker(sampler->loopStartParam, sampler->loopStartValue, 0.3f);
        setMarker(sampler->loopEndParam, sampler->loopEndValue, 0.6f);
        return sampler;
    }

    void testRelocatePreservesInterpretation() {
        beginTest("Relocating a sample keeps root note and trim/loop markers");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr, "Test edit must be created");
        if (!edit)
            return;

        auto original = testScratchDir().getNonexistentChildFile("relocate_source", ".wav");
        expect(writeTestWav(original), "Test sample must be written");

        te::Plugin::Ptr keepAlive;
        auto* sampler = prepare(*edit, original, keepAlive);
        if (sampler == nullptr)
            return;

        // Same audio, new home — exactly what a media migration does.
        auto moved = testScratchDir().getNonexistentChildFile("relocate_dest", ".wav");
        expect(original.moveFileTo(moved), "Sample must move");

        sampler->relocateSample(moved);

        expectEquals(sampler->getSampleFile().getFullPathName(), moved.getFullPathName(),
                     "Sampler must follow the file");
        expectEquals(sampler->getRootNote(), 48, "Root note must survive relocation");
        expectWithinAbsoluteError(sampler->sampleStartValue.get(), 0.25f, 0.0001f,
                                  "Sample start must survive relocation");
        expectWithinAbsoluteError(sampler->sampleEndValue.get(), 0.75f, 0.0001f,
                                  "Sample end must survive relocation");
        expectWithinAbsoluteError(sampler->loopStartValue.get(), 0.3f, 0.0001f,
                                  "Loop start must survive relocation");
        expectWithinAbsoluteError(sampler->loopEndValue.get(), 0.6f, 0.0001f,
                                  "Loop end must survive relocation");

        moved.deleteFile();
    }

    void testLoadSampleStillResets() {
        beginTest("Choosing a genuinely new sample still resets interpretation");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr, "Test edit must be created");
        if (!edit)
            return;

        auto original = testScratchDir().getNonexistentChildFile("reset_source", ".wav");
        expect(writeTestWav(original), "Test sample must be written");

        te::Plugin::Ptr keepAlive;
        auto* sampler = prepare(*edit, original, keepAlive);
        if (sampler == nullptr)
            return;

        // relocateSample narrows loadSample, it does not replace it: picking a
        // different sample must still start from a clean interpretation.
        auto other = testScratchDir().getNonexistentChildFile("reset_other", ".wav");
        expect(writeTestWav(other), "Second test sample must be written");

        sampler->loadSample(other);

        expectEquals(sampler->getRootNote(), 60, "A newly chosen sample resets the root note");
        expectWithinAbsoluteError(sampler->sampleStartValue.get(), 0.0f, 0.0001f,
                                  "A newly chosen sample resets the start marker");
        expectWithinAbsoluteError(sampler->loopStartValue.get(), 0.0f, 0.0001f,
                                  "A newly chosen sample resets the loop start");

        original.deleteFile();
        other.deleteFile();
    }
};

static SamplerRelocateTest samplerRelocateTest;
