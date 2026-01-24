#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <map>
#include <memory>

#include "../core/DeviceInfo.hpp"
#include "../core/TrackManager.hpp"
#include "../core/TypeIds.hpp"
#include "DeviceProcessor.hpp"
#include "MeteringBuffer.hpp"
#include "ParameterQueue.hpp"

namespace magda {

// Forward declarations
namespace te = tracktion;

/**
 * @brief Bridges TrackManager (UI model) to Tracktion Engine (audio processing)
 *
 * Responsibilities:
 * - Listens to TrackManager for device changes
 * - Maps DeviceId to tracktion::Plugin instances
 * - Maps TrackId to tracktion::AudioTrack instances
 * - Loads built-in and external plugins
 * - Manages metering and parameter communication
 *
 * Thread Safety:
 * - UI thread: Receives TrackManager notifications, updates mappings
 * - Audio thread: Reads mappings, processes parameter changes, pushes metering
 */
class AudioBridge : public TrackManagerListener, public juce::Timer {
  public:
    /**
     * @brief Construct AudioBridge with Tracktion Engine references
     * @param engine Reference to the Tracktion Engine instance
     * @param edit Reference to the current Edit (project)
     */
    AudioBridge(te::Engine& engine, te::Edit& edit);
    ~AudioBridge() override;

    // =========================================================================
    // TrackManagerListener implementation
    // =========================================================================

    void tracksChanged() override;
    void trackPropertyChanged(int trackId) override;
    void trackDevicesChanged(TrackId trackId) override;
    void devicePropertyChanged(DeviceId deviceId) override;

    // =========================================================================
    // Plugin Loading
    // =========================================================================

    /**
     * @brief Load a built-in Tracktion plugin
     * @param trackId The MAGDA track ID
     * @param type Plugin type (e.g., "tone", "volume", "delay", "reverb")
     * @return The loaded plugin, or nullptr on failure
     */
    te::Plugin::Ptr loadBuiltInPlugin(TrackId trackId, const juce::String& type);

    /**
     * @brief Load an external plugin (VST3, AU)
     * @param trackId The MAGDA track ID
     * @param description Plugin description from plugin scan
     * @return The loaded plugin, or nullptr on failure
     */
    te::Plugin::Ptr loadExternalPlugin(TrackId trackId, const juce::PluginDescription& description);

    /**
     * @brief Add a level meter plugin to a track for metering
     * @param trackId The MAGDA track ID
     * @return The level meter plugin
     */
    te::Plugin::Ptr addLevelMeterToTrack(TrackId trackId);

    // =========================================================================
    // Track Mapping
    // =========================================================================

    /**
     * @brief Get the Tracktion AudioTrack for a MAGDA track
     * @param trackId MAGDA track ID
     * @return The AudioTrack, or nullptr if not found
     */
    te::AudioTrack* getAudioTrack(TrackId trackId);

    /**
     * @brief Get the Tracktion Plugin for a MAGDA device
     * @param deviceId MAGDA device ID
     * @return The Plugin, or nullptr if not found
     */
    te::Plugin::Ptr getPlugin(DeviceId deviceId);

    /**
     * @brief Get the DeviceProcessor for a MAGDA device
     * @param deviceId MAGDA device ID
     * @return The DeviceProcessor, or nullptr if not found
     */
    DeviceProcessor* getDeviceProcessor(DeviceId deviceId);

    /**
     * @brief Create a Tracktion AudioTrack for a MAGDA track
     * @param trackId MAGDA track ID
     * @param name Track name
     * @return The created AudioTrack
     */
    te::AudioTrack* createAudioTrack(TrackId trackId, const juce::String& name);

    /**
     * @brief Remove a Tracktion track
     * @param trackId MAGDA track ID
     */
    void removeAudioTrack(TrackId trackId);

    // =========================================================================
    // Metering
    // =========================================================================

    /**
     * @brief Get the metering buffer for reading levels in UI
     */
    MeteringBuffer& getMeteringBuffer() {
        return meteringBuffer_;
    }
    const MeteringBuffer& getMeteringBuffer() const {
        return meteringBuffer_;
    }

    // =========================================================================
    // Parameter Queue
    // =========================================================================

    /**
     * @brief Get the parameter queue for pushing changes from UI
     */
    ParameterQueue& getParameterQueue() {
        return parameterQueue_;
    }

    /**
     * @brief Push a parameter change to the audio thread
     */
    bool pushParameterChange(DeviceId deviceId, int paramIndex, float value);

    // =========================================================================
    // Synchronization
    // =========================================================================

    /**
     * @brief Sync all tracks and devices to Tracktion Engine
     * Call this after initial setup or major state changes
     */
    void syncAll();

    /**
     * @brief Sync a single track's devices to Tracktion Engine
     */
    void syncTrackPlugins(TrackId trackId);

    // =========================================================================
    // Audio Callback Support
    // =========================================================================

    /**
     * @brief Process pending parameter changes (call from audio thread)
     */
    void processParameterChanges();

    /**
     * @brief Update metering from level measurers (call from audio thread)
     */
    void updateMetering();

  private:
    // Timer callback for metering updates (runs on message thread)
    void timerCallback() override;

    // Create track mapping
    void ensureTrackMapping(TrackId trackId);

    // Plugin creation helpers
    te::Plugin::Ptr createToneGenerator(te::AudioTrack* track);
    te::Plugin::Ptr createVolumeAndPan(te::AudioTrack* track);
    te::Plugin::Ptr createLevelMeter(te::AudioTrack* track);

    // Convert DeviceInfo to plugin
    te::Plugin::Ptr loadDeviceAsPlugin(TrackId trackId, const DeviceInfo& device);

    // References to Tracktion Engine (not owned)
    te::Engine& engine_;
    te::Edit& edit_;

    // Bidirectional mappings
    std::map<TrackId, te::AudioTrack*> trackMapping_;
    std::map<DeviceId, te::Plugin::Ptr> deviceToPlugin_;
    std::map<te::Plugin*, DeviceId> pluginToDevice_;

    // Device processors (own the processing logic for each device)
    std::map<DeviceId, std::unique_ptr<DeviceProcessor>> deviceProcessors_;

    // Per-track level measurer clients (needed to read levels)
    std::map<TrackId, te::LevelMeasurer::Client> meterClients_;

    // Lock-free communication buffers
    MeteringBuffer meteringBuffer_;
    ParameterQueue parameterQueue_;

    // Synchronization
    juce::CriticalSection mappingLock_;  // Protects mapping updates

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioBridge)
};

}  // namespace magda
