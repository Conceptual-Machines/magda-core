// Relocating a sampler's file must not re-interpret the sample (#2170).

#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include "JuceTestStateGuard.hpp"
#include "SharedTestEngine.hpp"
#include "magda/daw/audio/plugins/MagdaSamplerPlugin.hpp"
#include "magda/daw/audio/plugins/tracktion/SamplerHostBinding.hpp"
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

// Mirrors how the faceplate itself moves a marker: through the host wrapper's
// parameter, which is what the device reads its slot back from.
void setMarker(te::Plugin& plugin, int slot, float seconds) {
    magda::daw::audio::tracktion_adapter::setSamplerSlotDisplayValue(plugin, slot, seconds,
                                                                     juce::dontSendNotification);
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
        testLoadTakesMarkersIntoHostParameters();
        testRestoreWithoutLoopEnabledSwitchesItOff();
        testSameSourceRestoreLeavesTheMarkersAlone();
    }

  private:
    // A sampler holding `sampleFile` with deliberately non-default
    // interpretation settings, or nullptr if the fixture could not be built.
    MagdaSamplerPlugin* prepare(te::Edit& edit, const juce::File& sampleFile,
                                te::Plugin::Ptr& keepAlive) {
        keepAlive = createCustomPlugin(edit, MagdaSamplerPlugin::xmlTypeName);
        auto* sampler = magda::daw::audio::tracktion_adapter::deviceFromPlugin<MagdaSamplerPlugin>(
            keepAlive.get());
        expect(sampler != nullptr, "Sampler plugin must be created");
        if (sampler == nullptr)
            return nullptr;

        magda::daw::audio::tracktion_adapter::loadSamplerSample(*keepAlive, sampleFile);
        expectEquals(sampler->getSampleFile().getFullPathName(), sampleFile.getFullPathName(),
                     "Sampler must have adopted the sample");

        sampler->setRootNote(48);
        setMarker(*keepAlive, MagdaSamplerPlugin::kSampleStart, 0.25f);
        setMarker(*keepAlive, MagdaSamplerPlugin::kSampleEnd, 0.75f);
        setMarker(*keepAlive, MagdaSamplerPlugin::kLoopStart, 0.3f);
        setMarker(*keepAlive, MagdaSamplerPlugin::kLoopEnd, 0.6f);
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

        magda::daw::audio::tracktion_adapter::relocateSamplerSample(*keepAlive, moved);

        expectEquals(sampler->getSampleFile().getFullPathName(), moved.getFullPathName(),
                     "Sampler must follow the file");
        expectEquals(sampler->getRootNote(), 48, "Root note must survive relocation");
        expectWithinAbsoluteError(sampler->displayValue(MagdaSamplerPlugin::kSampleStart), 0.25f,
                                  0.0001f, "Sample start must survive relocation");
        expectWithinAbsoluteError(sampler->displayValue(MagdaSamplerPlugin::kSampleEnd), 0.75f,
                                  0.0001f, "Sample end must survive relocation");
        expectWithinAbsoluteError(sampler->displayValue(MagdaSamplerPlugin::kLoopStart), 0.3f,
                                  0.0001f, "Loop start must survive relocation");
        expectWithinAbsoluteError(sampler->displayValue(MagdaSamplerPlugin::kLoopEnd), 0.6f,
                                  0.0001f, "Loop end must survive relocation");

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

        magda::daw::audio::tracktion_adapter::loadSamplerSample(*keepAlive, other);

        expectEquals(sampler->getRootNote(), 60, "A newly chosen sample resets the root note");
        expectWithinAbsoluteError(sampler->displayValue(MagdaSamplerPlugin::kSampleStart), 0.0f,
                                  0.0001f, "A newly chosen sample resets the start marker");
        expectWithinAbsoluteError(sampler->displayValue(MagdaSamplerPlugin::kLoopStart), 0.0f,
                                  0.0001f, "A newly chosen sample resets the loop start");

        original.deleteFile();
        other.deleteFile();
    }

    void testLoadTakesMarkersIntoHostParameters() {
        beginTest("A sample load reaches the host's parameters, not only the device");

        // The device derives the trim markers from the audio it just loaded,
        // and the host is the authority every block: syncParametersToDevice()
        // pushes on every applyToBuffer. Without the pull back, the host's
        // stale markers overwrite the derived ones at the next render, and the
        // sample plays a region that belongs to whatever was loaded before.
        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr, "Test edit must be created");
        if (!edit)
            return;

        auto file = testScratchDir().getNonexistentChildFile("host_sync", ".wav");
        expect(writeTestWav(file), "Test sample must be written");

        te::Plugin::Ptr keepAlive;
        auto* sampler = prepare(*edit, file, keepAlive);
        if (sampler == nullptr)
            return;

        // prepare() moved the markers off the load's defaults; reloading the
        // same file resets them, which is the change that has to travel.
        magda::daw::audio::tracktion_adapter::loadSamplerSample(*keepAlive, file);

        auto* devicePlugin =
            dynamic_cast<magda::daw::audio::tracktion_adapter::TracktionMagdaDevicePlugin*>(
                keepAlive.get());
        expect(devicePlugin != nullptr, "The sampler must sit inside the host's device wrapper");
        if (devicePlugin == nullptr)
            return;

        for (const int index : {MagdaSamplerPlugin::kSampleStart, MagdaSamplerPlugin::kSampleEnd,
                                MagdaSamplerPlugin::kLoopStart, MagdaSamplerPlugin::kLoopEnd}) {
            auto* parameter = devicePlugin->parameterForDeviceSlot(index);
            expect(parameter != nullptr, "Every sampler slot must have a host parameter");
            if (parameter == nullptr)
                continue;

            const float fromHost = magda::ParameterUtils::normalizedToReal(
                parameter->getCurrentValue(), sampler->parameterInfo(index));
            expectWithinAbsoluteError(fromHost, sampler->displayValue(index), 0.0001f,
                                      "The host parameter must carry the marker the load derived");
        }

        file.deleteFile();
    }

    void testRestoreWithoutLoopEnabledSwitchesItOff() {
        beginTest("A restore that does not name loopEnabled switches looping off");

        // The document is the whole authored state, so a state saved before the
        // loop was switched on has to switch it back off. Keeping the previous
        // value leaves a pad looping that the project says does not (#2377).
        MagdaSamplerPlugin sampler;
        sampler.setLoopEnabled(true);

        juce::ValueTree state{juce::Identifier("PLUGIN")};
        state.setProperty(juce::Identifier("type"), MagdaSamplerPlugin::xmlTypeName, nullptr);
        sampler.restoreState(state);

        expect(!sampler.loopEnabled(), "An absent loopEnabled must restore as off");

        state.setProperty(MagdaSamplerPlugin::StateIDs::loopEnabled, true, nullptr);
        sampler.restoreState(state);

        expect(sampler.loopEnabled(), "A state that names loopEnabled must still restore it");
    }

    void testSameSourceRestoreLeavesTheMarkersAlone() {
        beginTest("Restoring the source already loaded leaves the markers alone");

        // An authored-state edit is projected as a whole document, so a loop
        // switch reaches the device as a restore naming the sample it already
        // holds. Re-reading the file there would cut every sounding voice and
        // re-derive markers the model owns (#2379).
        auto file = testScratchDir().getNonexistentChildFile("same_source", ".wav");
        expect(writeTestWav(file), "Test sample must be written");

        MagdaSamplerPlugin sampler;
        sampler.loadSample(file);
        sampler.setDisplayValue(MagdaSamplerPlugin::kSampleEnd, 0.4f);
        sampler.setDisplayValue(MagdaSamplerPlugin::kLoopEnd, 0.0f);

        juce::ValueTree state{juce::Identifier("PLUGIN")};
        state.setProperty(juce::Identifier("type"), MagdaSamplerPlugin::xmlTypeName, nullptr);
        state.setProperty(MagdaSamplerPlugin::StateIDs::source, file.getFullPathName(), nullptr);
        state.setProperty(MagdaSamplerPlugin::StateIDs::rootNote, 48, nullptr);
        state.setProperty(MagdaSamplerPlugin::StateIDs::loopEnabled, true, nullptr);
        sampler.restoreState(state);

        expect(sampler.loopEnabled(), "The switch the document names is still applied");
        expectEquals(sampler.getRootNote(), 48, "And so is the root note");
        expectWithinAbsoluteError(sampler.displayValue(MagdaSamplerPlugin::kSampleEnd), 0.4f,
                                  0.0001f, "The end marker must be left where it was");
        expectWithinAbsoluteError(sampler.displayValue(MagdaSamplerPlugin::kLoopEnd), 0.0f, 0.0001f,
                                  "A zero loop end must not be re-derived from the file");

        file.deleteFile();
    }
};

static SamplerRelocateTest samplerRelocateTest;
