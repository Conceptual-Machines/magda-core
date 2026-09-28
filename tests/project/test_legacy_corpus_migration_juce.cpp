#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <vector>

#include "LegacyCorpus.hpp"
#include "SharedTestEngine.hpp"
#include "magda/daw/audio/plugins/DeviceServices.hpp"
#include "magda/daw/audio/plugins/InternalPluginRegistry.hpp"
#include "magda/daw/audio/plugins/tracktion/TracktionDeviceStateBridge.hpp"
#include "magda/daw/audio/plugins/tracktion/TracktionMagdaDevicePlugin.hpp"
#include "magda/daw/core/DeviceState.hpp"
#include "magda/daw/core/ParameterUtils.hpp"
#include "magda/daw/core/RackInfo.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"

// -----------------------------------------------------------------------------
// pluginState v1 -> v2 over the legacy corpus (#2079)
//
// #1887 replaced the engine's own plugin ValueTree with a MAGDA-owned document
// and shipped a reader for the old shape. This is that reader run against every
// device state in the corpus - real states written by builds from 1.0.0 through
// 0.17, not states this build produced to test itself with.
//
// The conversion needs a live plugin: v1 is the engine's vocabulary, and the
// only thing that can turn it into parameter values is the device itself. So
// the model-level corpus (test_legacy_corpus.cpp) asserts the string is kept
// verbatim, and this asserts what happens when an engine finally reads it:
//
//   - the legacy state builds a plugin, and capture returns a v2 document;
//   - the document's parameter values are the plugin's, keyed by frozen index;
//   - restoring that document rebuilds the same plugin, value for value;
//   - capturing a second time returns the same document, so opening and saving
//     an old project twice does not keep changing the file.
// -----------------------------------------------------------------------------

namespace {

namespace te = tracktion::engine;
namespace audio = magda::daw::audio;
namespace bridge = magda::daw::audio::tracktion_adapter;
namespace ds = magda::device_state;
namespace corpus = magda::test::legacy_corpus;

class CorpusDeviceServices final : public audio::DeviceIdAllocator,
                                   public audio::DeviceTrackContext {
  public:
    magda::DeviceId allocateDeviceId() override {
        return nextDeviceId_++;
    }
    void ensureDeviceIdAbove(magda::DeviceId id) override {
        nextDeviceId_ = std::max(nextDeviceId_, id + 1);
    }
    void setDeviceParameterValueFromPlugin(magda::DeviceId, int, float) override {}

  private:
    magda::DeviceId nextDeviceId_ = 9000;
};

/// A device in the corpus that still carries pre-v2 engine state.
struct LegacyDevice {
    juce::String fixture;
    juce::String trackName;
    magda::DeviceInfo device;
};

std::vector<LegacyDevice> collectLegacyDevices() {
    std::vector<LegacyDevice> found;

    for (const auto& entry : corpus::projectFixtures()) {
        if (entry.legacyDeviceStates == 0)
            continue;

        magda::StagedProjectData staged;
        if (!magda::ProjectSerializer::loadAndStage(corpus::projectsDir().getChildFile(entry.file),
                                                    staged))
            continue;

        const auto collect = [&](const magda::TrackInfo& track) {
            corpus::forEachDevice(track, [&](const magda::DeviceInfo& device) {
                if (ds::looksLikeLegacyEngineState(device.pluginState))
                    found.push_back({entry.file, track.name, device});
            });
        };

        for (const auto& track : staged.tracks)
            collect(track);
        if (staged.masterTrack != nullptr)
            collect(*staged.masterTrack);
    }

    return found;
}

std::vector<float> parameterValues(te::Plugin& plugin) {
    // In the same domain the document stores: devices behind the host adapter
    // capture DISPLAY values (their normalised slots converted through the
    // device's ParameterInfo), everything else captures the parameter's own
    // range directly.
    const auto* adapter =
        dynamic_cast<magda::daw::audio::tracktion_adapter::TracktionMagdaDevicePlugin*>(&plugin);
    const bool internalWrapper = adapter != nullptr && magda::daw::audio::findInternalPluginSpec(
                                                           plugin.getPluginType()) != nullptr;

    std::vector<float> values;
    const auto& params = plugin.getAutomatableParameters();
    for (int i = 0; i < params.size(); ++i) {
        auto* param = params[i];
        float value = param != nullptr ? param->getCurrentValue() : 0.0f;
        if (param != nullptr && internalWrapper)
            value =
                magda::ParameterUtils::normalizedToReal(value, adapter->device().parameterInfo(i));
        values.push_back(value);
    }
    return values;
}

class LegacyDeviceStateMigrationTest final : public juce::UnitTest {
  public:
    LegacyDeviceStateMigrationTest()
        : juce::UnitTest("Legacy Corpus Device State Migration", "magda") {}

    void runTest() override {
        beginTest("The shared engine provides an edit to build devices in");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr);
        if (edit == nullptr)
            return;

        CorpusDeviceServices testServices;
        audio::DeviceServices services;
        services.deviceIdAllocator = &testServices;
        services.trackContext = &testServices;
        const auto sessionKey = audio::DeviceSessionKey::fromAddress(edit.get());
        audio::registerDeviceServices(sessionKey, services);

        const auto legacyDevices = collectLegacyDevices();

        beginTest("The corpus still carries pre-v2 device state to migrate");
        expect(!legacyDevices.empty(),
               "no legacy device state found in the corpus - the fixtures or the loader changed");

        beginTest("Pre-v2 device state converts to a v2 document");
        int converted = 0;
        for (const auto& legacy : legacyDevices) {
            const auto where =
                legacy.fixture + " / " + legacy.trackName + " / " + legacy.device.pluginId;

            auto tree = bridge::devicePluginTreeFromState(legacy.device.pluginState);
            if (!tree.isValid()) {
                expect(false, "legacy state produced no plugin tree: " + where);
                continue;
            }

            auto plugin = edit->getPluginCache().createNewPlugin(tree);
            if (plugin == nullptr) {
                // A device this build cannot instantiate (an optional pack that
                // is off, or an external plugin that is not installed here).
                logMessage("not instantiable in this build, unchecked: " + where);
                continue;
            }

            const auto captured =
                bridge::captureInternalDeviceState(*plugin, legacy.device.pluginState);
            if (captured.isEmpty()) {
                logMessage("device has no capturable state: " + where);
                plugin->deleteFromParent();
                continue;
            }

            expect(ds::isDeviceStateV2(captured), "capture did not produce v2: " + where);
            expect(!ds::looksLikeLegacyEngineState(captured),
                   "capture wrote engine XML back out: " + where);

            const auto decoded = ds::decode(captured);
            expect(decoded.has_value(), "v2 document does not decode: " + where);
            if (!decoded)
                continue;

            // #2317: the converted document is authored state only. The
            // parameter values a legacy project carries restore from its
            // serialized DeviceInfo::parameters array (hydrated at load), not
            // from this document.
            expect(decoded->params.empty(),
                   "conversion wrote a duplicate parameter record: " + where);

            // Restore from the v2 document into a fresh plugin: the authored
            // state round-trips and a second save does not change the file.
            auto restoredTree = bridge::devicePluginTreeFromState(captured);
            expect(restoredTree.isValid(), "v2 document produced no plugin tree: " + where);
            if (restoredTree.isValid()) {
                if (auto restored = edit->getPluginCache().createNewPlugin(restoredTree)) {
                    const auto restoredValues = parameterValues(*restored);
                    const auto live = parameterValues(*plugin);
                    expectEquals(static_cast<int>(restoredValues.size()),
                                 static_cast<int>(live.size()),
                                 "restored plugin has a different parameter count: " + where);

                    const auto recaptured = bridge::captureInternalDeviceState(*restored, captured);
                    expectEquals(recaptured, captured,
                                 "a second capture differs from the first: " + where);

                    restored->deleteFromParent();
                }
            }

            ++converted;
            plugin->deleteFromParent();
        }

        expect(converted > 0, "no legacy device state was converted in this build");
        logMessage("converted " + juce::String(converted) + " legacy device states");

        audio::unregisterDeviceServices(sessionKey);
    }
};

LegacyDeviceStateMigrationTest legacyDeviceStateMigrationTest;

}  // namespace
