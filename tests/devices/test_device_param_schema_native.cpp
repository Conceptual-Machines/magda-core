#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>
#include <map>

#include "magda/daw/audio/plugins/DeviceCatalogParameters.hpp"
#include "magda/daw/audio/plugins/InternalPluginRegistry.hpp"
#include "magda/daw/audio/plugins/MagdaDevice.hpp"
#include "magda/daw/audio/plugins/compiled/CompiledPluginRegistry.hpp"

// The frozen parameter order (#1887) as the native engine sees it (#2556, ported from
// the fork's suite). Saved automation, macro, mod and MIDI links persist a parameter's
// position, so every device the native engine builds must list the same ids in the same
// order as tests/device_param_schema.txt. The id is the parameter's stableId, or
// `<pluginId>_param_<index>` when it has none, which is the rule the fork's adapter uses.
// Devices the native engine does not build on their own (the fork's built-ins) are
// skipped, as the file already allows for optional packs.

namespace {

namespace audio = magda::daw::audio;

std::map<juce::String, juce::String> frozenSchema() {
    std::map<juce::String, juce::String> lines;
    juce::StringArray fileLines;
    fileLines.addLines(juce::File(MAGDA_DEVICE_PARAM_SCHEMA_FILE).loadFileAsString());
    for (const auto& line : fileLines)
        if (line.isNotEmpty())
            lines[line.upToFirstOccurrenceOf(" ", false, false)] = line;
    return lines;
}

juce::String parameterLine(const juce::String& pluginId, const audio::MagdaDevice& device) {
    juce::StringArray ids;
    const auto parameters = device.parameters();
    for (int index = 0; index < static_cast<int>(parameters.size()); ++index) {
        const auto& info = parameters[static_cast<std::size_t>(index)];
        const int stableIndex = info.paramIndex >= 0 ? info.paramIndex : index;
        ids.add(info.stableId.isNotEmpty() ? info.stableId
                                           : pluginId + "_param_" + juce::String(stableIndex));
    }
    juce::String line = pluginId + " " + juce::String(ids.size());
    if (!ids.isEmpty())
        line << " " << ids.joinIntoString(",");
    return line;
}

}  // namespace

TEST_CASE("Native devices keep the frozen parameter order", "[devices][param-schema][2556]") {
    const auto frozen = frozenSchema();
    REQUIRE_FALSE(frozen.empty());

    juce::StringArray pluginIds;
    for (const auto* spec : audio::getAllInternalPluginSpecs())
        if (spec != nullptr && spec->pluginId != nullptr && spec->canCreateDetached &&
            spec->createMode != audio::InternalPluginCreateMode::Unsupported &&
            !audio::internalPluginHasTag(*spec, "null-diff-corpus"))
            pluginIds.add(spec->pluginId);
    for (const auto* spec : audio::compiled::getAllCompiledPluginSpecs())
        if (spec != nullptr && spec->pluginId != nullptr)
            pluginIds.add(spec->pluginId);

    int checked = 0;
    for (const auto& pluginId : pluginIds) {
        INFO(pluginId);
        auto device = audio::createDetachedDevice(pluginId);
        if (device == nullptr)
            continue;
        const auto entry = frozen.find(pluginId);
        REQUIRE(entry != frozen.end());
        CHECK(parameterLine(pluginId, *device) == entry->second);
        ++checked;
    }
    CHECK(checked > 0);
}
