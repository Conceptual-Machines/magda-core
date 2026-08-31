#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include "SharedTestEngine.hpp"
#include "magda/daw/audio/plugins/DeviceServices.hpp"
#include "magda/daw/audio/plugins/DrumGridPlugin.hpp"
#include "magda/daw/audio/plugins/MidiChordEnginePlugin.hpp"
#include "magda/daw/audio/plugins/MidiReceivePlugin.hpp"
#include "magda/daw/audio/plugins/OscilloscopePlugin.hpp"
#include "magda/daw/audio/plugins/SpectrumAnalyzerPlugin.hpp"
#include "magda/daw/audio/plugins/compiled/CompiledFaustInterface.hpp"
#include "magda/daw/audio/plugins/compiled/tracktion/CompiledFaustTracktionAdapter.hpp"
#include "magda/daw/audio/plugins/tracktion/TracktionMagdaDevicePlugin.hpp"
#include "magda/daw/audio/processors/CompiledFaustProcessor.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"

namespace {

namespace audio = magda::daw::audio;
namespace te = tracktion::engine;

te::Plugin::Ptr createCustomPlugin(te::Edit& edit, const juce::String& type) {
    juce::ValueTree state(te::IDs::PLUGIN);
    state.setProperty(te::IDs::type, type, nullptr);
    return edit.getPluginCache().createNewPlugin(state);
}

te::Plugin::Ptr createCustomPlugin(te::Edit& edit, juce::ValueTree state) {
    return edit.getPluginCache().createNewPlugin(state);
}

class TestDeviceServices final : public audio::DeviceIdAllocator, public audio::DeviceTrackContext {
  public:
    magda::DeviceId allocateDeviceId() override {
        return nextDeviceId++;
    }

    void ensureDeviceIdAbove(magda::DeviceId id) override {
        nextDeviceId = std::max(nextDeviceId, id + 1);
    }

    bool isChordTrackMuted() const override {
        ++chordMuteReads;
        return chordTrackMuted;
    }

    void setDeviceParameterValueFromPlugin(magda::DeviceId deviceId, int paramIndex,
                                           float value) override {
        lastDeviceId = deviceId;
        lastParamIndex = paramIndex;
        lastValue = value;
    }

    magda::DeviceId nextDeviceId = 700;
    bool chordTrackMuted = true;
    mutable int chordMuteReads = 0;
    magda::DeviceId lastDeviceId = magda::INVALID_DEVICE_ID;
    int lastParamIndex = -1;
    float lastValue = 0.0f;
};

class DeviceServicesInjectionTest final : public juce::UnitTest {
  public:
    DeviceServicesInjectionTest() : juce::UnitTest("Device Services Injection", "magda") {}

    void runTest() override {
        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);

        beginTest("Custom device constructors receive per-edit services and defaults");
        expect(edit != nullptr);
        if (edit == nullptr)
            return;

        TestDeviceServices testServices;
        audio::DeviceServices services;
        services.deviceIdAllocator = &testServices;
        services.trackContext = &testServices;
        services.defaults.oscilloscope.timebaseMs = 321.0f;
        services.defaults.spectrum.fftOrder = 12;
        services.defaults.spectrum.slopeDbPerOct = 2.25f;
        services.defaults.spectrum.smoothing = 0.75f;
        services.defaults.midiReceive.sourceTrackId = 42;
        services.defaults.midiReceive.replaceExistingMidi = true;
        audio::registerDeviceServices(audio::DeviceSessionKey::fromAddress(edit.get()), services);

        {
            // A Drum Grid comes up with no pads and allocates no ids of its
            // own: which pads exist and what sits on them is model state, and
            // it is filled from there (#2207).
            auto drumPlugin = createCustomPlugin(*edit, audio::DrumGridPlugin::xmlTypeName);
            auto* drumGrid = dynamic_cast<audio::DrumGridPlugin*>(drumPlugin.get());
            expect(drumGrid != nullptr);
            if (drumGrid != nullptr)
                expect(drumGrid->getChains().empty());

            auto oscPlugin = createCustomPlugin(*edit, audio::OscilloscopePlugin::xmlTypeName);
            auto* oscilloscope =
                audio::tracktion_adapter::deviceFromPlugin<audio::OscilloscopePlugin>(
                    oscPlugin.get());
            expect(oscilloscope != nullptr);
            if (oscilloscope != nullptr)
                expectWithinAbsoluteError(oscilloscope->getTimebaseMs(), 321.0f, 0.001f);

            auto spectrumPlugin =
                createCustomPlugin(*edit, audio::SpectrumAnalyzerPlugin::xmlTypeName);
            auto* spectrum =
                audio::tracktion_adapter::deviceFromPlugin<audio::SpectrumAnalyzerPlugin>(
                    spectrumPlugin.get());
            expect(spectrum != nullptr);
            if (spectrum != nullptr) {
                expectEquals(spectrum->getFftOrder(), 12);
                expectWithinAbsoluteError(spectrum->getSlopeDbPerOct(), 2.25f, 0.001f);
                expectWithinAbsoluteError(spectrum->getSmoothing(), 0.75f, 0.001f);
            }

            juce::ValueTree savedScopeState(te::IDs::PLUGIN);
            savedScopeState.setProperty(te::IDs::type, audio::OscilloscopePlugin::xmlTypeName,
                                        nullptr);
            savedScopeState.setProperty("traceColour", 6, nullptr);
            savedScopeState.setProperty("timebaseMs", 987.0f, nullptr);
            auto restoredScopePlugin = createCustomPlugin(*edit, savedScopeState);
            auto* restoredScope =
                audio::tracktion_adapter::deviceFromPlugin<audio::OscilloscopePlugin>(
                    restoredScopePlugin.get());
            expect(restoredScope != nullptr);
            if (restoredScope != nullptr) {
                expectEquals(restoredScope->getTraceColourIndex(), 6);
                expectWithinAbsoluteError(restoredScope->getTimebaseMs(), 987.0f, 0.001f);
            }

            juce::ValueTree savedSpectrumState(te::IDs::PLUGIN);
            savedSpectrumState.setProperty(te::IDs::type,
                                           audio::SpectrumAnalyzerPlugin::xmlTypeName, nullptr);
            savedSpectrumState.setProperty("traceColour", 4, nullptr);
            savedSpectrumState.setProperty("fftOrder", 11, nullptr);
            savedSpectrumState.setProperty("slopeDbPerOct", 1.5f, nullptr);
            savedSpectrumState.setProperty("smoothing", 0.25f, nullptr);
            auto restoredSpectrumPlugin = createCustomPlugin(*edit, savedSpectrumState);
            auto* restoredSpectrum =
                audio::tracktion_adapter::deviceFromPlugin<audio::SpectrumAnalyzerPlugin>(
                    restoredSpectrumPlugin.get());
            expect(restoredSpectrum != nullptr);
            if (restoredSpectrum != nullptr) {
                expectEquals(restoredSpectrum->getTraceColourIndex(), 4);
                expectEquals(restoredSpectrum->getFftOrder(), 11);
                expectWithinAbsoluteError(restoredSpectrum->getSlopeDbPerOct(), 1.5f, 0.001f);
                expectWithinAbsoluteError(restoredSpectrum->getSmoothing(), 0.25f, 0.001f);
            }

            auto compiledPlugin = createCustomPlugin(*edit, "magda_kick");
            auto* compiled =
                audio::tracktion_adapter::deviceFromPlugin<audio::compiled::ICompiledFaustPlugin>(
                    compiledPlugin.get());
            auto* compiledAdapter =
                dynamic_cast<audio::tracktion_adapter::TracktionMagdaDevicePlugin*>(
                    compiledPlugin.get());
            expect(compiled != nullptr);
            expect(compiledAdapter != nullptr);
            if (compiled != nullptr && compiledAdapter != nullptr) {
                auto* hostParameter =
                    audio::compiled::tracktionParameterForSlot(compiledPlugin.get(), 0);
                expect(hostParameter != nullptr);
                expect(hostParameter == compiledAdapter->parameterForDeviceSlot(0));

                magda::CompiledFaustProcessor processor(701, compiledPlugin);
                constexpr float targetNormalized = 0.73f;
                const float targetDisplay = compiled->normalizedToDisplay(0, targetNormalized);
                processor.setParameterByIndex(0, targetDisplay);
                expectWithinAbsoluteError(hostParameter->getCurrentBaseValue(), targetNormalized,
                                          0.0001f);

                // Corrupt the device-side mirror, then run the adapter's
                // start-of-block sync. The Tracktion parameter must remain
                // authoritative and restore the mirror, not snap back.
                compiled->hostSlotParameter(0).setValueFromHost(0.11f);
                te::PluginRenderContext renderContext(
                    nullptr, {}, 0, 0, nullptr, 0.0,
                    tracktion::TimeRange(tracktion::TimePosition::fromSeconds(0.0),
                                         tracktion::TimePosition::fromSeconds(0.0)),
                    false, false, false, false);
                compiledAdapter->applyToBuffer(renderContext);
                expectWithinAbsoluteError(hostParameter->getCurrentBaseValue(), targetNormalized,
                                          0.0001f);
                expectWithinAbsoluteError(compiled->hostSlotParameter(0).currentValue(),
                                          targetNormalized, 0.0001f);
                expectWithinAbsoluteError(processor.getParameterByIndex(0), targetDisplay, 0.0001f);
            }

            auto midiReceivePlugin =
                createCustomPlugin(*edit, magda::MidiReceivePlugin::xmlTypeName);
            auto* midiReceive = dynamic_cast<magda::MidiReceivePlugin*>(midiReceivePlugin.get());
            expect(midiReceive != nullptr);
            if (midiReceive != nullptr) {
                expectEquals(midiReceive->getSourceTrackId(), 42);
                expect(midiReceive->getReplaceExistingMidi());
            }

            auto chordPlugin = createCustomPlugin(*edit, audio::MidiChordEnginePlugin::xmlTypeName);
            expect(dynamic_cast<audio::MidiChordEnginePlugin*>(chordPlugin.get()) != nullptr);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
            expect(testServices.chordMuteReads > 0);

#if defined(MAGDA_PRO_DEVICES_ENABLED) && MAGDA_PRO_DEVICES_ENABLED
            auto proPlugin = createCustomPlugin(*edit, "magda-pro-stub");
            expect(proPlugin != nullptr);
            if (proPlugin != nullptr) {
                expectEquals(proPlugin->getName(), juce::String("Pro Pack Stub"));
                expectEquals(proPlugin->getPluginType(), juce::String("magda-pro-stub"));
            }
#endif
        }

        audio::unregisterDeviceServices(audio::DeviceSessionKey::fromAddress(edit.get()));
    }
};

DeviceServicesInjectionTest deviceServicesInjectionTest;

}  // namespace
