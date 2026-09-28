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
//
// The file is the UNION across build configurations. Optional device packs mean
// the roster is not fixed: MAGDA_PRO_DEVICES is OFF by default and ON in CI, so
// `magda-pro-stub` exists only in some builds. A device in the file that this
// build cannot instantiate is therefore not an error - the build that does have
// it checks its order. A device this build CAN instantiate but that is missing
// from the file still is.
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

    // No trailing separator for a device with no parameters: the repo's
    // trailing-whitespace hook would strip it from the committed file, and every
    // zero-parameter device would then fail the comparison forever.
    juce::String line = deviceType + " " + juce::String(ids.size());
    if (!ids.isEmpty())
        line << " " << ids.joinIntoString(",");
    return line;
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

        // The null-diff corpus registers its own devices so that both engines
        // can create one (NullDiffTeLeg.cpp). They live only inside this test
        // binary, and this file is the product's contract about parameter
        // order for saved automation and links: freezing a test fixture in it
        // would put a corpus device under a promise made to projects on disk.
        if (audio::internalPluginHasTag(*spec, "null-diff-corpus"))
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

juce::String deviceTypeOf(const juce::String& line) {
    return line.upToFirstOccurrenceOf(" ", false, false);
}

/// The frozen file is the UNION of every build configuration's device roster, so
/// regenerating from one configuration must not drop the devices another one
/// contributes. Live lines win; untouched entries are carried over verbatim.
juce::StringArray mergedSchema(const juce::StringArray& live, const juce::File& existingFile) {
    juce::StringArray merged = live;

    if (existingFile.existsAsFile()) {
        juce::StringArray existing;
        existing.addLines(existingFile.loadFileAsString());
        existing.removeEmptyStrings();

        for (const auto& line : existing) {
            const auto deviceType = deviceTypeOf(line);
            const bool replacedByLive =
                std::any_of(live.begin(), live.end(), [&](const juce::String& candidate) {
                    return deviceTypeOf(candidate) == deviceType;
                });
            if (!replacedByLive)
                merged.add(line);
        }
    }

    merged.sort(false);
    return merged;
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
            // Merge, never replace. Optional device packs mean the roster depends
            // on the build configuration (MAGDA_PRO_DEVICES is OFF by default and
            // ON in CI), so a wholesale rewrite from one configuration would drop
            // every device the other one contributes and the file would ping-pong.
            // Explicit LF: replaceWithText defaults to CRLF, which would make the
            // committed file churn between platforms.
            file.replaceWithText(mergedSchema(live, file).joinIntoString("\n") + "\n", false, false,
                                 "\n");
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
            const auto deviceType = deviceTypeOf(frozenLine);
            const auto* liveLine =
                std::find_if(live.begin(), live.end(), [&](const juce::String& candidate) {
                    return deviceTypeOf(candidate) == deviceType;
                });

            if (liveLine == live.end()) {
                // Not a schema break: the file is the union across build
                // configurations, and this one does not build that device (an
                // optional pack is off). Its order is checked by the build that
                // does have it.
                logMessage("not built in this configuration, order unchecked here: " + deviceType);
                continue;
            }

            expectEquals(*liveLine, frozenLine,
                         "parameter order changed for '" + deviceType +
                             "'. Saved automation, macro and mod links are keyed on this order - "
                             "append new parameters at the end, or write a migration.");
        }

        // The other direction stays fatal: a device this build CAN instantiate
        // must be recorded, or adding one would quietly escape the freeze.
        for (const auto& liveLine : live) {
            const auto deviceType = deviceTypeOf(liveLine);
            const bool known =
                std::any_of(frozen.begin(), frozen.end(), [&](const juce::String& candidate) {
                    return deviceTypeOf(candidate) == deviceType;
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
