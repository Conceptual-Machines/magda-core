#include <set>
#include <unordered_set>
#include <vector>

#include "../../core/RackInfo.hpp"
#include "../../core/TrackManager.hpp"
#include "../../core/aliases/AutoAliasGenerator.hpp"
#include "../../profiling/PerformanceProfiler.hpp"
#include "../PluginWindowBridge.hpp"
#include "../TrackController.hpp"
#include "../TracktionHelpers.hpp"
#include "PluginManager.hpp"
#include "modifiers/CurveSnapshot.hpp"
#include "modifiers/ModifierHelpers.hpp"
#include "modifiers/ModifierSync.hpp"
#include "plugins/ArpeggiatorPlugin.hpp"
#include "plugins/AudioSidechainMonitorPlugin.hpp"
#include "plugins/DrumGridPlugin.hpp"
#include "plugins/MagdaSamplerPlugin.hpp"
#include "plugins/MidiChordEnginePlugin.hpp"
#include "plugins/MidiReceivePlugin.hpp"
#include "plugins/SidechainMonitorPlugin.hpp"
#include "plugins/StepSequencerPlugin.hpp"
#include "transport/TransportStateManager.hpp"

namespace magda {

void PluginManager::syncRackProperties(TrackId trackId) {
    auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    if (!trackInfo)
        return;

    for (const auto& element : trackInfo->chain.fxChainElements) {
        if (isRack(element)) {
            rackSyncManager_.updateRackProperties(getRack(element));
        }
    }
}

// =============================================================================
// Macro Value Routing
// =============================================================================

void PluginManager::setMacroValue(TrackId trackId, ChainScope scope, int ownerId, int macroIndex,
                                  float value) {
    juce::ignoreUnused(trackId);
    switch (scope) {
        case ChainScope::Rack:
            rackSyncManager_.setMacroValue(static_cast<RackId>(ownerId), macroIndex, value);
            return;
        case ChainScope::Track: {
            auto tmIt = trackMacroParams_.find(static_cast<TrackId>(ownerId));
            if (tmIt == trackMacroParams_.end())
                return;
            auto macroIt = tmIt->second.find(macroIndex);
            if (macroIt != tmIt->second.end() && macroIt->second != nullptr)
                macroIt->second->setParameterFromHost(value, juce::sendNotificationSync);
            return;
        }
        case ChainScope::Device:
            setMacroValue(ChainNodePath::topLevelDevice(trackId, static_cast<DeviceId>(ownerId)),
                          macroIndex, value);
            return;
    }
}

void PluginManager::setMacroValue(const ChainNodePath& devicePath, int macroIndex, float value) {
    auto it = findSyncedDevice(devicePath);
    if (it == syncedDevices_.end())
        return;

    auto macroIt = it->second.macroParams.find(macroIndex);
    if (macroIt != it->second.macroParams.end() && macroIt->second != nullptr) {
        macroIt->second->setParameterFromHost(value, juce::sendNotificationSync);
    }
}

te::AutomatableParameter* PluginManager::findMacroParameterForAutomation(
    TrackId trackId, const ChainNodePath& devicePath, int macroIndex) const {
    if (macroIndex < 0)
        return nullptr;

    if (devicePath.isValid()) {
        switch (devicePath.getType()) {
            case ChainNodeType::Rack:
                return rackSyncManager_.findRackMacroParameter(devicePath.getRackId(), macroIndex);
            case ChainNodeType::TopLevelDevice:
            case ChainNodeType::Device: {
                auto it = findSyncedDevice(devicePath);
                if (it == syncedDevices_.end())
                    return nullptr;
                auto macroIt = it->second.macroParams.find(macroIndex);
                if (macroIt == it->second.macroParams.end() || macroIt->second == nullptr)
                    return nullptr;
                return macroIt->second;
            }
            default:
                break;
        }
    }

    // Track-scope macro fallback.
    auto tmIt = trackMacroParams_.find(trackId);
    if (tmIt != trackMacroParams_.end()) {
        auto macroIt = tmIt->second.find(macroIndex);
        if (macroIt != tmIt->second.end() && macroIt->second != nullptr)
            return macroIt->second;
    }
    return nullptr;
}

}  // namespace magda
