#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <algorithm>
#include <vector>

#include "SharedTestEngine.hpp"
#include "magda/daw/audio/plugins/DeviceServices.hpp"
#include "magda/daw/audio/plugins/InternalPluginRegistry.hpp"
#include "magda/daw/audio/plugins/compiled/CompiledPluginRegistry.hpp"
#include "magda/daw/audio/plugins/tracktion/TracktionInternalPluginAdapter.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"

// -----------------------------------------------------------------------------
// Frozen paramIndex schema (#1887)
//
// A device's parameter ORDER is a compatibility surface, not an implementation
// detail. Automation lanes, macro links, mod links and MIDI bindings all persist
// `ControlTarget::paramIndex`, which is the position of the parameter in the
// device's automatable list. Inserting a parameter anywhere but the end, or
// reordering two, silently re-points every saved link on every existing project.
//
// This test pins that order. `tests/device_param_schema.txt` records, per device
// type, the parameter ids in index order. Any change to the live devices has to
// show up as a deliberate edit to that file.
//
// To regenerate after an intentional change:
//   MAGDA_WRITE_DEVICE_PARAM_SCHEMA=1 \
//     cmake-build-debug/tests/magda_juce_tests_artefacts/Debug/magda_juce_tests \
//     "Device Param Schema Freeze"
//
// Appending a parameter to the end of a device is safe and only needs the file
// regenerated. Anything else needs a migration.
// -----------------------------------------------------------------------------

namespace {

namespace audio = magda::daw::audio;
namespace te = tracktion::engine;

class SchemaDeviceServices final : public audio::DeviceIdAllocator,
                                   public audio::DeviceTrackContext {
  public:
    magda::DeviceId allocateDeviceId() override {
        return nextDeviceId_++;
    }
    void ensureDeviceIdAbove(magda::DeviceId id) override {
        nextDeviceId_ = std::max(nextDeviceId_, id + 1);
    }
    bool isChordTrackMuted() const override {
        return false;
    }
    void setDeviceParameterValueFromPlugin(magda::DeviceId, int, float) override {}

  private:
    magda::DeviceId nextDeviceId_ = 9000;
};

juce::File schemaFile() {
    return juce::File(MAGDA_DEVICE_PARAM_SCHEMA_FILE);
}

bool shouldRewrite() {
    return juce::SystemStats::getEnvironmentVariable("MAGDA_WRITE_DEVICE_PARAM_SCHEMA", {})
        .isNotEmpty();
}

juce::String parameterLine(const juce::String& deviceType, te::Plugin& plugin) {
    juce::StringArray ids;
    for (auto* param : plugin.getAutomatableParameters())
        ids.add(param != nullptr ? param->paramID : juce::String("<null>"));

    return deviceType + " " + juce::String(ids.size()) + " " + ids.joinIntoString(",");
}

/// Every device the registries expose that can be instantiated on its own.
/// Infrastructure devices (MIDI receive, monitors, taps) are deliberately out of
/// scope: they carry no user-visible parameters and no saved links.
juce::StringArray collectSchema(te::Edit& edit) {
    juce::StringArray lines;

    for (const auto* spec : audio::getAllInternalPluginSpecs()) {
        if (spec == nullptr || spec->pluginId == nullptr)
            continue;
        if (!spec->canCreateDetached ||
            spec->createMode == audio::InternalPluginCreateMode::Unsupported)
            continue;

        auto plugin = audio::tracktion_adapter::createInternalPlugin(*spec, edit);
        if (plugin == nullptr)
            continue;

        lines.add(parameterLine(spec->pluginId, *plugin));
        plugin->deleteFromParent();
    }

    // Compiled Faust devices are not in the internal registry: they are created
    // straight from a typed plugin tree, the same way PluginManager does it.
    for (const auto* spec : audio::compiled::getAllCompiledPluginSpecs()) {
        if (spec == nullptr || spec->pluginId == nullptr)
            continue;

        juce::ValueTree state(te::IDs::PLUGIN);
        state.setProperty(te::IDs::type, spec->pluginId, nullptr);
        auto plugin = edit.getPluginCache().createNewPlugin(state);
        if (plugin == nullptr)
            continue;

        lines.add(parameterLine(spec->pluginId, *plugin));
        plugin->deleteFromParent();
    }

    lines.sort(false);
    return lines;
}

class DeviceParamSchemaFreezeTest final : public juce::UnitTest {
  public:
    DeviceParamSchemaFreezeTest() : juce::UnitTest("Device Param Schema Freeze", "magda") {}

    void runTest() override {
        beginTest("Device parameter order matches the frozen schema");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr);
        if (edit == nullptr)
            return;

        SchemaDeviceServices testServices;
        audio::DeviceServices services;
        services.deviceIdAllocator = &testServices;
        services.trackContext = &testServices;
        const auto sessionKey = audio::DeviceSessionKey::fromAddress(edit.get());
        audio::registerDeviceServices(sessionKey, services);

        const auto live = collectSchema(*edit);
        expect(!live.isEmpty(), "no instantiable devices found - registry not populated?");

        const auto file = schemaFile();

        if (shouldRewrite()) {
            // Explicit LF: replaceWithText defaults to CRLF, which would make the
            // committed file churn between platforms every time it is regenerated.
            file.replaceWithText(live.joinIntoString("\n") + "\n", false, false, "\n");
            logMessage("Rewrote " + file.getFullPathName());
        }

        if (!file.existsAsFile()) {
            expect(false, "missing freeze file: " + file.getFullPathName() +
                              " (regenerate with MAGDA_WRITE_DEVICE_PARAM_SCHEMA=1)");
            audio::unregisterDeviceServices(sessionKey);
            return;
        }

        juce::StringArray frozen;
        frozen.addLines(file.loadFileAsString());
        frozen.removeEmptyStrings();

        // Compare device by device so a failure names the device that moved
        // rather than dumping the whole table.
        for (const auto& frozenLine : frozen) {
            const auto deviceType = frozenLine.upToFirstOccurrenceOf(" ", false, false);
            const auto* liveLine =
                std::find_if(live.begin(), live.end(), [&](const juce::String& candidate) {
                    return candidate.upToFirstOccurrenceOf(" ", false, false) == deviceType;
                });

            if (liveLine == live.end()) {
                // A device that no longer instantiates is not a schema break by
                // itself (it may be gone on purpose), but it must be a
                // deliberate edit to the freeze file.
                expect(false, "device missing from this build: " + deviceType +
                                  " (remove it from the freeze file if intentional)");
                continue;
            }

            expectEquals(*liveLine, frozenLine,
                         "parameter order changed for '" + deviceType +
                             "'. Saved automation, macro and mod links are keyed on this order - "
                             "append new parameters at the end, or write a migration.");
        }

        for (const auto& liveLine : live) {
            const auto deviceType = liveLine.upToFirstOccurrenceOf(" ", false, false);
            const bool known =
                std::any_of(frozen.begin(), frozen.end(), [&](const juce::String& candidate) {
                    return candidate.upToFirstOccurrenceOf(" ", false, false) == deviceType;
                });
            expect(known, "new device '" + deviceType +
                              "' is not in the freeze file (regenerate with "
                              "MAGDA_WRITE_DEVICE_PARAM_SCHEMA=1)");
        }

        audio::unregisterDeviceServices(sessionKey);
    }
};

DeviceParamSchemaFreezeTest deviceParamSchemaFreezeTest;

}  // namespace
