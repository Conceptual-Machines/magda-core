#include "AutoAliasGenerator.hpp"

#include "../TrackManager.hpp"
#include "ParamNameNormalize.hpp"

namespace magda {

// ============================================================================
// pluginNameToAlias -- maps a plugin display name to a canonical alias key
// ============================================================================

static juce::String pluginNameToAlias(const juce::String& pluginName) {
    return normalizeParamName(pluginName);
}

// ============================================================================
// computeForDevice
// ============================================================================

std::map<juce::String, StoredAlias> AutoAliasGenerator::computeForDevice(
    const DeviceInfo& deviceInfo, const ChainNodePath& devicePath) {
    std::map<juce::String, StoredAlias> result;

    if (deviceInfo.parameters.empty())
        return result;

    const juce::String pluginKey = pluginNameToAlias(deviceInfo.name);
    if (pluginKey.isEmpty())
        return result;

    // Build normalized param names for all parameters.
    std::vector<juce::String> rawKeys;
    rawKeys.reserve(deviceInfo.parameters.size());
    for (const auto& param : deviceInfo.parameters)
        rawKeys.push_back(normalizeParamName(param.name));

    // Make keys unique (appends _2, _3, ... on duplicates).
    const std::vector<juce::String> uniqueKeys = uniquify(rawKeys);

    for (int i = 0; i < static_cast<int>(deviceInfo.parameters.size()); ++i) {
        const auto& param = deviceInfo.parameters[static_cast<size_t>(i)];
        const juce::String& paramKey = uniqueKeys[static_cast<size_t>(i)];

        if (paramKey.isEmpty())
            continue;

        juce::String aliasName = pluginKey + "." + paramKey;

        StoredAlias alias;
        alias.pluginTypeKey = pluginKey;
        alias.paramIndex = param.paramIndex;
        alias.paramNameAtSetTime = param.name;
        alias.path = devicePath;

        result[aliasName] = alias;
    }

    return result;
}

// ============================================================================
// regenerateForDevice
// ============================================================================

void AutoAliasGenerator::regenerateForDevice(DeviceId deviceId) {
    auto& tm = TrackManager::getInstance();

    // Find the device path in the track tree.
    ChainNodePath devicePath = tm.findDevicePath(deviceId);
    if (!devicePath.isValid())
        return;

    regenerateForDevice(devicePath);
}

void AutoAliasGenerator::regenerateForDevice(const ChainNodePath& devicePath) {
    auto& tm = TrackManager::getInstance();

    // Retrieve the DeviceInfo (parameters must already be populated).
    const DeviceInfo* devInfo = tm.getDeviceInChainByPath(devicePath);
    if (devInfo == nullptr)
        return;

    auto entries = computeForDevice(*devInfo, devicePath);
    AliasRegistry::getInstance().replaceAutoForDevice(devicePath, std::move(entries));
}

}  // namespace magda
