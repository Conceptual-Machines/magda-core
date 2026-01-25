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
    void masterChannelChanged() override;

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

    // =========================================================================
    // Transport State (for trigger sync)
    // =========================================================================

    /**
     * @brief Update transport state from UI thread (called by TracktionEngineWrapper)
     * @param isPlaying Current transport playing state
     * @param justStarted True if transport just started this frame
     * @param justLooped True if transport just looped this frame
     */
    void updateTransportState(bool isPlaying, bool justStarted, bool justLooped);

    /**
     * @brief Get current transport playing state (audio thread safe)
     */
    bool isTransportPlaying() const {
        return transportPlaying_.load(std::memory_order_acquire);
    }

    /**
     * @brief Get just-started flag (audio thread safe)
     */
    bool didJustStart() const {
        return justStartedFlag_.load(std::memory_order_acquire);
    }

    /**
     * @brief Get just-looped flag (audio thread safe)
     */
    bool didJustLoop() const {
        return justLoopedFlag_.load(std::memory_order_acquire);
    }

    // =========================================================================
    // MIDI Activity Monitoring
    // =========================================================================

    /**
     * @brief Trigger MIDI activity for a track (audio thread safe)
     * @param trackId The track that received MIDI
     */
    void triggerMidiActivity(TrackId trackId);

    /**
     * @brief Check and clear MIDI activity flag for a track (UI thread)
     * @param trackId The track to check
     * @return true if MIDI activity occurred since last check
     */
    bool consumeMidiActivity(TrackId trackId);

    // =========================================================================
    // Mixer Controls
    // =========================================================================

    /**
     * @brief Set track volume (linear gain, 0.0-2.0)
     * @param trackId MAGDA track ID
     * @param volume Linear gain (0.0 = silence, 1.0 = unity, 2.0 = +6dB)
     */
    void setTrackVolume(TrackId trackId, float volume);

    /**
     * @brief Get track volume (linear gain)
     * @param trackId MAGDA track ID
     * @return Linear gain value
     */
    float getTrackVolume(TrackId trackId) const;

    /**
     * @brief Set track pan
     * @param trackId MAGDA track ID
     * @param pan Pan position (-1.0 = full left, 0.0 = center, 1.0 = full right)
     */
    void setTrackPan(TrackId trackId, float pan);

    /**
     * @brief Get track pan
     * @param trackId MAGDA track ID
     * @return Pan position
     */
    float getTrackPan(TrackId trackId) const;

    /**
     * @brief Set master volume (linear gain)
     * @param volume Linear gain (0.0 = silence, 1.0 = unity, 2.0 = +6dB)
     */
    void setMasterVolume(float volume);

    /**
     * @brief Get master volume (linear gain)
     * @return Linear gain value
     */
    float getMasterVolume() const;

    /**
     * @brief Set master pan
     * @param pan Pan position (-1.0 to 1.0)
     */
    void setMasterPan(float pan);

    /**
     * @brief Get master pan
     * @return Pan position
     */
    float getMasterPan() const;

    // =========================================================================
    // Master Metering
    // =========================================================================

    /**
     * @brief Get master channel peak level (left)
     * @return Peak level as linear gain
     */
    float getMasterPeakL() const {
        return masterPeakL_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Get master channel peak level (right)
     * @return Peak level as linear gain
     */
    float getMasterPeakR() const {
        return masterPeakR_.load(std::memory_order_relaxed);
    }

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
     * @brief Set audio input source for a track
     * @param trackId The MAGDA track ID
     * @param deviceId Input device ID, "default" for default input, empty to disable
     */
    void setTrackAudioInput(TrackId trackId, const juce::String& deviceId);

    /**
     * @brief Get current audio output destination for a track
     * @return Output destination string
     */
    juce::String getTrackAudioOutput(TrackId trackId) const;

    /**
     * @brief Get current audio input source for a track
     * @return Input device ID
     */
    juce::String getTrackAudioInput(TrackId trackId) const;

  private:
    // Timer callback for metering updates (runs on message thread)
    void timerCallback() override;

    // Create track mapping
    void ensureTrackMapping(TrackId trackId);

    // Plugin creation helpers
    te::Plugin::Ptr createToneGenerator(te::AudioTrack* track);
    te::Plugin::Ptr createVolumeAndPan(te::AudioTrack* track);
    te::Plugin::Ptr createLevelMeter(te::AudioTrack* track);
    te::Plugin::Ptr createFourOscSynth(te::AudioTrack* track);

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

    // Transport state (UI thread writes, audio thread reads - lock-free)
    std::atomic<bool> transportPlaying_{false};
    std::atomic<bool> justStartedFlag_{false};
    std::atomic<bool> justLoopedFlag_{false};

    // MIDI activity flags (audio thread writes, UI thread reads/clears - lock-free)
    static constexpr int kMaxTracks = 128;
    std::array<std::atomic<bool>, kMaxTracks> midiActivityFlags_;

    // Master channel metering (lock-free atomics for thread safety)
    std::atomic<float> masterPeakL_{0.0f};
    std::atomic<float> masterPeakR_{0.0f};
    te::LevelMeasurer::Client masterMeterClient_;
    bool masterMeterRegistered_{false};  // Whether master meter client is registered

    // Synchronization
    juce::CriticalSection mappingLock_;  // Protects mapping updates

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioBridge)
};

}  // namespace magda
