#pragma once

#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <functional>
#include <map>
#include <vector>

#include "../core/TypeIds.hpp"

namespace magda {

// Forward declarations
namespace te = tracktion;

/**
 * @brief Manages track lifecycle, mapping, mixer controls, and audio routing
 *
 * Responsibilities:
 * - Core track lifecycle (create, remove, lookup, ensure mapping)
 * - Track mapping (TrackId → TE AudioTrack*)
 * - Mixer controls (volume and pan for tracks)
 * - Audio routing (input/output device assignment)
 * - Metering coordination (owns meterClients_ for AudioBridge)
 * - Thread-safe iteration over track mapping
 *
 * Thread Safety:
 * - All operations protected by internal trackLock_
 * - createAudioTrack() uses single lock pattern for thread safety
 * - withTrackMapping() provides lock-protected callback iteration
 * - withMeterClients() provides lock-protected callback iteration
 *
 * Dependencies:
 * - te::Engine& (for device management)
 * - te::Edit& (for track creation/deletion)
 */
class TrackController {
  public:
    /**
     * @brief Construct TrackController with engine and edit references
     * @param engine Reference to the Tracktion Engine instance
     * @param edit Reference to the current Edit (project)
     */
    TrackController(te::Engine& engine, te::Edit& edit);

    // =========================================================================
    // Core Track Lifecycle
    // =========================================================================

    /**
     * @brief Get the Tracktion AudioTrack for a MAGDA track
     * @param trackId MAGDA track ID
     * @return The AudioTrack, or nullptr if not found
     *
     * WARNING: Returns raw pointer under lock. Pointer may become invalid if track is deleted
     * after this call returns. Caller must ensure track lifetime or use immediately.
     */
    te::AudioTrack* getAudioTrack(TrackId trackId) const;

    /**
     * @brief Create a Tracktion AudioTrack for a MAGDA track
     * @param trackId MAGDA track ID
     * @param name Track name
     * @return The created AudioTrack (or existing if already created)
     */
    te::AudioTrack* createAudioTrack(TrackId trackId, const juce::String& name);

    /**
     * @brief Remove a Tracktion track
     * @param trackId MAGDA track ID
     */
    void removeAudioTrack(TrackId trackId);

    /**
     * @brief Ensure a track mapping exists (idempotent creation)
     * @param trackId MAGDA track ID
     * @param name Track name (used only if track doesn't exist)
     * @return The AudioTrack (existing or newly created)
     */
    te::AudioTrack* ensureTrackMapping(TrackId trackId, const juce::String& name);

    // =========================================================================
    // Mixer Controls
    // =========================================================================

    /**
     * @brief Set track volume (linear gain)
     * @param trackId MAGDA track ID
     * @param volume Linear gain (0.0 = silence, 1.0 = unity, 2.0 = +6dB)
     */
    void setTrackVolume(TrackId trackId, float volume);

    /**
     * @brief Get track volume (linear gain)
     * @param trackId MAGDA track ID
     * @return Linear gain value (1.0 = unity)
     */
    float getTrackVolume(TrackId trackId) const;

    /**
     * @brief Set track pan position
     * @param trackId MAGDA track ID
     * @param pan Pan position (-1.0 = full left, 0.0 = center, 1.0 = full right)
     */
    void setTrackPan(TrackId trackId, float pan);

    /**
     * @brief Get track pan position
     * @param trackId MAGDA track ID
     * @return Pan position (-1.0 to 1.0, 0.0 = center)
     */
    float getTrackPan(TrackId trackId) const;

    // =========================================================================
    // Audio Routing
    // =========================================================================

    /**
     * @brief Set audio output destination for a track
     * @param trackId The MAGDA track ID
     * @param destination Output destination: "master" for default, deviceID for specific output,
     * empty to disable
     */
    void setTrackAudioOutput(TrackId trackId, const juce::String& destination);

    /**
     * @brief Get current audio output destination for a track
     * @param trackId The MAGDA track ID
     * @return Output destination string
     */
    juce::String getTrackAudioOutput(TrackId trackId) const;

    /**
     * @brief Set audio input source for a track
     * @param trackId The MAGDA track ID
     * @param deviceId Input device ID, "default" for default input, empty to disable
     */
    void setTrackAudioInput(TrackId trackId, const juce::String& deviceId);

    /**
     * @brief Get current audio input source for a track
     * @param trackId The MAGDA track ID
     * @return Input device ID
     */
    juce::String getTrackAudioInput(TrackId trackId) const;

    // =========================================================================
    // Utilities
    // =========================================================================

    /**
     * @brief Get all mapped track IDs
     * @return Vector of all TrackIds currently mapped
     */
    std::vector<TrackId> getAllTrackIds() const;

    /**
     * @brief Clear all track mappings and meter clients
     * Called during shutdown to clean up state
     */
    void clearAllMappings();

    /**
     * @brief Execute a callback with thread-safe access to track mapping
     * @param callback Function to execute with const reference to trackMapping_
     *
     * Usage: trackController.withTrackMapping([&](const auto& mapping) { ... });
     */
    void withTrackMapping(
        std::function<void(const std::map<TrackId, te::AudioTrack*>&)> callback) const;

    // =========================================================================
    // Metering Coordination (for PluginManager)
    // =========================================================================

    /**
     * @brief Add a meter client for a track (thread-safe)
     * @param trackId The track ID
     * @param levelMeter The LevelMeterPlugin to register with
     */
    void addMeterClient(TrackId trackId, te::LevelMeterPlugin* levelMeter);

    /**
     * @brief Remove meter client for a track (thread-safe)
     * @param trackId The track ID
     * @param levelMeter The LevelMeterPlugin to unregister from (optional)
     */
    void removeMeterClient(TrackId trackId, te::LevelMeterPlugin* levelMeter = nullptr);

    /**
     * @brief Execute a callback with thread-safe access to meter clients
     * @param callback Function to execute with const reference to meterClients_
     *
     * Used by AudioBridge for meter updates in timer thread
     */
    void withMeterClients(
        std::function<void(const std::map<TrackId, te::LevelMeasurer::Client>&)> callback) const;

  private:
    // References to Tracktion Engine (not owned)
    te::Engine& engine_;
    te::Edit& edit_;

    // Track mapping and metering state
    std::map<TrackId, te::AudioTrack*> trackMapping_;
    std::map<TrackId, te::LevelMeasurer::Client> meterClients_;

    // Thread safety
    mutable juce::CriticalSection trackLock_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackController)
};

}  // namespace magda
