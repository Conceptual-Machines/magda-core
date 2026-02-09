#pragma once

#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <functional>
#include <map>
#include <memory>

#include "../core/DeviceInfo.hpp"
#include "../core/TypeIds.hpp"
#include "DeviceProcessor.hpp"
#include "InstrumentRackManager.hpp"

namespace magda {

// Forward declarations
namespace te = tracktion;
class TrackController;
class PluginWindowBridge;
class TransportStateManager;

/**
 * @brief Result of attempting to load a plugin
 */
struct PluginLoadResult {
    bool success = false;
    juce::String errorMessage;
    te::Plugin::Ptr plugin;

    static PluginLoadResult Success(const te::Plugin::Ptr& p) {
        return {true, {}, p};
    }
    static PluginLoadResult Failure(const juce::String& msg) {
        return {false, msg, nullptr};
    }
};

/**
 * @brief Manages plugin/device synchronization and lifecycle
 *
 * Responsibilities:
 * - Plugin/device mapping (DeviceId ↔ TE Plugin)
 * - Device processor management
 * - Plugin synchronization from TrackManager
 * - Built-in and external plugin loading
 * - Device → Plugin conversion
 *
 * Thread Safety:
 * - All operations protected by internal pluginLock_
 * - Lookup methods are lock-protected
 *
 * Dependencies:
 * - te::Engine& (for plugin cache, known plugin list)
 * - te::Edit& (for plugin cache)
 * - TrackController& (for track lookup/creation)
 * - PluginWindowBridge& (for closing plugin windows)
 * - TransportStateManager& (for tone generator bypass state)
 */
class PluginManager {
  public:
    /**
     * @brief Construct PluginManager with required dependencies
     * @param engine Reference to the Tracktion Engine instance
     * @param edit Reference to the current Edit (project)
     * @param trackController Reference to TrackController for track operations
     * @param pluginWindowBridge Reference to PluginWindowBridge for window management
     * @param transportState Reference to TransportStateManager for transport state
     */
    PluginManager(te::Engine& engine, te::Edit& edit, TrackController& trackController,
                  PluginWindowBridge& pluginWindowBridge, TransportStateManager& transportState);

    // =========================================================================
    // Plugin/Device Lookup
    // =========================================================================

    /**
     * @brief Get the Tracktion Plugin for a MAGDA device
     * @param deviceId MAGDA device ID
     * @return The Plugin, or nullptr if not found
     */
    te::Plugin::Ptr getPlugin(DeviceId deviceId) const;

    /**
     * @brief Get the DeviceProcessor for a MAGDA device
     * @param deviceId MAGDA device ID
     * @return The DeviceProcessor, or nullptr if not found
     */
    DeviceProcessor* getDeviceProcessor(DeviceId deviceId) const;

    // =========================================================================
    // Plugin Synchronization
    // =========================================================================

    /**
     * @brief Sync a track's plugins from TrackManager to Tracktion Engine
     * @param trackId The MAGDA track ID
     *
     * - Removes TE plugins that no longer exist in TrackManager
     * - Adds new plugins for MAGDA devices without TE counterparts
     * - Ensures VolumeAndPan and LevelMeter are positioned correctly
     */
    void syncTrackPlugins(TrackId trackId);

    // =========================================================================
    // Plugin Loading
    // =========================================================================

    /**
     * @brief Load a built-in Tracktion plugin
     * @param trackId The MAGDA track ID
     * @param type Plugin type (e.g., "tone", "delay", "reverb", "eq", "compressor")
     * @return The loaded plugin, or nullptr on failure
     */
    te::Plugin::Ptr loadBuiltInPlugin(TrackId trackId, const juce::String& type);

    /**
     * @brief Load an external plugin (VST3, AU)
     * @param trackId The MAGDA track ID
     * @param description Plugin description from plugin scan
     * @return PluginLoadResult with success status, error message, and plugin pointer
     */
    PluginLoadResult loadExternalPlugin(TrackId trackId,
                                        const juce::PluginDescription& description);

    /**
     * @brief Add a level meter plugin to a track for metering
     * @param trackId The MAGDA track ID
     * @return The level meter plugin
     */
    te::Plugin::Ptr addLevelMeterToTrack(TrackId trackId);

    /**
     * @brief Ensure VolumeAndPanPlugin is at the correct position (near end of chain)
     * @param track The Tracktion Engine audio track
     */
    void ensureVolumePluginPosition(te::AudioTrack* track) const;

    /**
     * @brief Callback invoked when a plugin fails to load
     * Parameters: deviceId, error message
     */
    std::function<void(DeviceId, const juce::String&)> onPluginLoadFailed;

    // =========================================================================
    // Utilities
    // =========================================================================

    /**
     * @brief Clear all plugin mappings and processors
     * Called during shutdown to clean up state
     */
    void clearAllMappings();

    /**
     * @brief Update transport-synced processors (e.g., tone generators)
     * @param isPlaying Current transport playing state
     *
     * Called by AudioBridge when transport state changes to update
     * processors that need to sync with transport (bypass when stopped)
     */
    void updateTransportSyncedProcessors(bool isPlaying);

  private:
    // Internal device → plugin conversion (used by syncTrackPlugins)
    te::Plugin::Ptr loadDeviceAsPlugin(TrackId trackId, const DeviceInfo& device);

    // Plugin creation helpers
    te::Plugin::Ptr createToneGenerator(te::AudioTrack* track);
    te::Plugin::Ptr createLevelMeter(te::AudioTrack* track);
    te::Plugin::Ptr createFourOscSynth(te::AudioTrack* track);

    // References to dependencies (not owned)
    te::Engine& engine_;
    te::Edit& edit_;
    TrackController& trackController_;
    PluginWindowBridge& pluginWindowBridge_;
    TransportStateManager& transportState_;

    // Instrument rack wrapping (synth + audio passthrough)
    InstrumentRackManager instrumentRackManager_;

    // Plugin/device mappings and processors
    std::map<DeviceId, te::Plugin::Ptr> deviceToPlugin_;
    std::map<te::Plugin*, DeviceId> pluginToDevice_;
    std::map<DeviceId, std::unique_ptr<DeviceProcessor>> deviceProcessors_;

    // Thread safety
    mutable juce::CriticalSection pluginLock_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginManager)
};

}  // namespace magda
