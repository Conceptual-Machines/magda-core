#pragma once

#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../core/ClipManager.hpp"
#include "../../core/TrackManager.hpp"
#include "../../core/TypeIds.hpp"
#include "ClipEngineIdMap.hpp"
#include "ClipWarpSynchronizer.hpp"

namespace magda {

// Forward declarations
namespace te = tracktion;
class TrackController;
class WarpMarkerManager;
struct WarpMarkerInfo;

/**
 * @brief Manages clip synchronization between ClipManager and Tracktion Engine
 *
 * Responsibilities:
 * - Bidirectional clip ID mapping (ClipId ↔ TE EditItemID)
 * - ClipManagerListener implementation (clips changed, property changed)
 * - Arrangement clip synchronization (audio + MIDI)
 * - Session clip slot management (create, launch, stop)
 * - Warp marker delegation to WarpMarkerManager
 *
 * Thread Safety:
 * - All operations assumed to run on message thread
 * - Clip mapping is isolated in ClipEngineIdMap
 * - pendingReverseClipId_ accessed from timer thread
 *
 * Dependencies:
 * - te::Edit& (for clip creation, tempo sequence, playback context)
 * - TrackController& (for track lookup and creation)
 * - WarpMarkerManager& (for transient detection and warp markers)
 */
class ClipSynchronizer : public ClipManagerListener, public TrackManagerListener {
  public:
    /**
     * @brief Construct ClipSynchronizer with required dependencies
     * @param edit Reference to the current Edit (project)
     * @param trackController Reference to TrackController for track operations
     * @param warpMarkerManager Reference to WarpMarkerManager for warp operations
     */
    ClipSynchronizer(te::Edit& edit, TrackController& trackController,
                     WarpMarkerManager& warpMarkerManager);

    /**
     * @brief Destructor - unregisters from ClipManager listener
     */
    ~ClipSynchronizer() override;

    // =========================================================================
    // ClipManagerListener Interface
    // =========================================================================

    /**
     * @brief Handle clip additions, deletions, and reordering
     *
     * - Removes clips that no longer exist in ClipManager
     * - Syncs all arrangement clips to TE
     * - Syncs all session clips to slots
     */
    void clipsChanged() override;

    /**
     * @brief Handle individual clip property changes
     * @param clipId The clip whose properties changed
     *
     * Routes to appropriate sync method based on clip view type
     */
    void clipPropertyChanged(ClipId clipId) override;

    /**
     * @brief Handle batched clip property changes synchronously
     * @param clipIds The clips whose properties changed
     *
     * Deduplicates IDs and coalesces playback graph reallocation while still
     * leaving TE state up to date before returning.
     */
    void clipPropertiesChanged(const std::vector<ClipId>& clipIds) override;

    /**
     * @brief Handle clip selection changes (no-op)
     * @param clipId The newly selected clip
     */
    void clipSelectionChanged(ClipId clipId) override;

    // =========================================================================
    // TrackManagerListener Interface
    // =========================================================================

    void tracksChanged() override {}
    void trackPropertyChanged(int trackId) override;

    // =========================================================================
    // Arrangement Clip Operations
    // =========================================================================

    /**
     * @brief Sync a clip from ClipManager to Tracktion Engine
     * @param clipId The MAGDA clip ID
     *
     * Routes to syncMidiClipToEngine() or syncAudioClipToEngine() based on type
     */
    void syncClipToEngine(ClipId clipId);

    /**
     * @brief Remove a clip from Tracktion Engine
     * @param clipId The MAGDA clip ID
     *
     * Removes from TE timeline and clears bidirectional mapping
     */
    void removeClipFromEngine(ClipId clipId);

    /**
     * @brief Get Tracktion Engine clip for arrangement clip
     * @param clipId The MAGDA clip ID
     * @return The TE Clip, or nullptr if not found
     */
    te::Clip* getArrangementTeClip(ClipId clipId) const;

    // =========================================================================
    // Session Clip Operations
    // =========================================================================

    /**
     * @brief Sync a session clip to its slot in TE Edit
     * @param clipId The MAGDA clip ID
     * @return true if a new clip was created (requires graph reallocation)
     *
     * Creates or updates clip in session slot, handles audio and MIDI clips
     */
    bool syncSessionClipToSlot(ClipId clipId);

    /**
     * @brief Remove a session clip from its slot
     * @param clipId The MAGDA clip ID
     */
    void removeSessionClipFromSlot(ClipId clipId);

    /**
     * @brief Launch a session clip for playback
     * @param clipId The MAGDA clip ID
     *
     * Configures looping and launches via LaunchHandle
     */
    void launchSessionClip(ClipId clipId, bool forceImmediate = false);

    /**
     * @brief Stop a playing session clip immediately
     * @param clipId The MAGDA clip ID
     */
    void stopSessionClip(ClipId clipId);

    /**
     * @brief Stop a playing session clip at the next quantization grid point
     * @param clipId The MAGDA clip ID
     * @param quantize The quantization to use for the stop
     */
    void stopSessionClipQueued(ClipId clipId, LaunchQuantize quantize);

    /**
     * @brief Get Tracktion Engine clip for session clip
     * @param clipId The MAGDA clip ID
     * @return The TE Clip in the slot, or nullptr if not found
     */
    te::Clip* getSessionTeClip(ClipId clipId);

    // =========================================================================
    // Warp Marker Operations (Delegated to WarpMarkerManager)
    // =========================================================================

    /**
     * @brief Set transient detection sensitivity and re-run detection
     * @param clipId The MAGDA clip ID
     * @param sensitivity Sensitivity value (0.0 to 1.0)
     */
    void setTransientSensitivity(ClipId clipId, float sensitivity);

    /**
     * @brief Get transient detection times for a clip
     * @param clipId The MAGDA clip ID
     * @return true if transients were found
     */
    bool getTransientTimes(ClipId clipId);

    /**
     * @brief Enable warp/time-stretch for a clip
     * @param clipId The MAGDA clip ID
     */
    void enableWarp(ClipId clipId);

    /**
     * @brief Disable warp/time-stretch for a clip
     * @param clipId The MAGDA clip ID
     */
    void disableWarp(ClipId clipId);

    /**
     * @brief Get all warp markers for a clip
     * @param clipId The MAGDA clip ID
     * @return Vector of warp marker information
     */
    std::vector<WarpMarkerInfo> getWarpMarkers(ClipId clipId);

    /**
     * @brief Add a warp marker to a clip
     * @param clipId The MAGDA clip ID
     * @param sourceTime Time in source audio
     * @param warpTime Warped time position
     * @return Index of the added marker
     */
    int addWarpMarker(ClipId clipId, double sourceTime, double warpTime);

    /**
     * @brief Move an existing warp marker
     * @param clipId The MAGDA clip ID
     * @param markerIndex Index of marker to move
     * @param newWarpTime New warped time position
     * @return Actual new warp time (may be clamped)
     */
    double moveWarpMarker(ClipId clipId, int markerIndex, double newWarpTime);

    /**
     * @brief Remove a warp marker from a clip
     * @param clipId The MAGDA clip ID
     * @param markerIndex Index of marker to remove
     */
    void removeWarpMarker(ClipId clipId, int markerIndex);

    // =========================================================================
    // Utilities
    // =========================================================================

    /**
     * @brief Callback fired after the playback graph is reallocated.
     * Used by AudioBridge to re-establish MIDI routing and input monitor state.
     */
    std::function<void()> onGraphReallocated;

    /**
     * @brief Check if a reverse proxy operation is pending
     * @return ClipId of pending clip, or INVALID_CLIP_ID
     *
     * Called from AudioBridge timer to poll for completion
     */
    ClipId getPendingReverseClipId() const {
        return pendingReverseClipId_;
    }

    /**
     * @brief Clear pending reverse clip ID after proxy completion
     */
    void clearPendingReverseClipId() {
        pendingReverseClipId_ = INVALID_CLIP_ID;
    }

    /**
     * @brief Get the precise quantized launch time for a track's last-launched session clip.
     * @param trackId The track to query
     * @return Time in seconds, or 0.0 if no launch recorded
     */
    double getLastLaunchTimeForTrack(TrackId trackId) const;

    /** Resolve an arrangement clip's Tracktion Engine item ID. */
    std::optional<std::string> getArrangementEngineId(ClipId clipId) const;

  private:
    // =========================================================================
    // Private Sync Helpers
    // =========================================================================

    /** Reallocate playback graph and fire onGraphReallocated callback. */
    void reallocateAndNotify();

    /**
     * @brief Sync one clip property notification into Tracktion Engine
     * @return true if this sync changed topology requiring graph reallocation
     */
    bool syncClipPropertyToEngine(ClipId clipId);

    /**
     * @brief Sync arrangement clip data and report whether graph reallocation is needed
     */
    bool syncArrangementClipToEngine(ClipId clipId);

    /**
     * @brief Sync MIDI clip properties to Tracktion Engine
     * @param clipId The MAGDA clip ID
     * @param clip The ClipInfo from ClipManager
     *
     * Handles position, looping, offset, and note data synchronization
     */
    bool syncMidiClipToEngine(ClipId clipId, const ClipInfo* clip);

    /**
     * @brief Sync audio clip properties to Tracktion Engine
     * @param clipId The MAGDA clip ID
     * @param clip The ClipInfo from ClipManager
     *
     * Handles position, speed, tempo sync, loop, offset, pitch, fades, etc.
     * Complex logic for beat-based vs. time-based properties
     */
    bool syncAudioClipToEngine(ClipId clipId, const ClipInfo* clip);

    /**
     * @brief Configure autoTempo on a session audio clip in TE
     * @param audioClip The TE WaveAudioClip to configure
     * @param clip The ClipInfo model data
     *
     * Shared by syncSessionClipToSlot() and clipPropertyChanged().
     * Syncs source interpretation BPM, stretch mode, speedRatio, autoTempo flag,
     * offset, and beat-based loop range.
     */
    void configureSessionAutoTempo(te::WaveAudioClip* audioClip, const ClipInfo* clip);

    /**
     * @brief Sync TrackInfo::playbackMode to TE's audioTrack->playSlotClips
     * @param trackId The track to sync
     *
     * This is the SINGLE place that writes audioTrack->playSlotClips.
     */
    void syncPlaybackModeToEngine(TrackId trackId);

    /**
     * @brief Remove a TE clip by its engine ID from any track
     * @param engineId The TE EditItemID string
     */
    void removeTeClipByEngineId(const std::string& engineId);

    // References to dependencies (not owned)
    te::Edit& edit_;
    TrackController& trackController_;

    ClipEngineIdMap clipIds_;
    ClipWarpSynchronizer warpSync_;

    // Reverse proxy state (for deferred reallocation)
    ClipId pendingReverseClipId_{INVALID_CLIP_ID};

    // Precise quantized launch times per track (seconds), written by launchSessionClip()
    std::unordered_map<TrackId, double> lastLaunchTimeByTrack_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipSynchronizer)
};

}  // namespace magda
