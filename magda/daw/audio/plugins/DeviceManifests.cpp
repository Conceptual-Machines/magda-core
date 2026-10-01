#include "plugins/DeviceManifests.hpp"

#include <set>

#include "plugins/BaseDevicePack.hpp"
#include "plugins/DeviceCatalogParameters.hpp"
#include "plugins/InternalPluginRegistry.hpp"
#include "plugins/MagdaDevice.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio {

namespace {

DeviceManifestEntry skipped(const juce::String& pluginId, const char* reason) {
    return {.pluginId = pluginId, .manifest = std::nullopt, .skipReason = reason};
}

DeviceManifestEntry describe(const juce::String& pluginId) {
    const auto device = createDetachedDevice(pluginId);
    if (device == nullptr)
        return skipped(pluginId, "no device factory; the host builds it");

    return {.pluginId = pluginId, .manifest = sdk::buildManifest(*device), .skipReason = {}};
}

}  // namespace

std::vector<DeviceManifestEntry> buildBasePackManifests() {
    // Naming the registration keeps the base pack linked when nothing else here does.
    [[maybe_unused]] const auto baseRegistration = &registerBaseDevices;

    std::vector<DeviceManifestEntry> entries;
    std::set<juce::String> seen;
    const auto add = [&](const juce::String& pluginId, DeviceManifestEntry entry) {
        if (seen.insert(pluginId).second)
            entries.push_back(std::move(entry));
    };

    for (const auto* spec : getAllInternalPluginSpecs()) {
        if (spec == nullptr || spec->pluginId == nullptr ||
            internalPluginHasTag(*spec, "null-diff-corpus"))
            continue;

        const juce::String pluginId = spec->pluginId;
        if (!spec->canCreateDetached || spec->createMode == InternalPluginCreateMode::Unsupported)
            add(pluginId, skipped(pluginId, "created only on a track, never detached"));
        else
            add(pluginId, describe(pluginId));
    }

    for (const auto* spec : compiled::getAllCompiledPluginSpecs())
        if (spec != nullptr && spec->pluginId != nullptr)
            add(spec->pluginId, describe(spec->pluginId));

    return entries;
}

}  // namespace magda::daw::audio
