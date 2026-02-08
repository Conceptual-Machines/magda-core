#pragma once

#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <map>
#include <string>
#include <vector>

#include "../core/ClipManager.hpp"
#include "../core/TypeIds.hpp"

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
 * - Clip mapping protected by internal clipLock_
 * - pendingReverseClipId_ accessed from timer thread
 *
 * Dependencies:
 * - te::Edit& (for clip creation, tempo sequence, playback context)
 * - TrackController& (for track lookup and creation)
 * - WarpMarkerManager& (for transient detection and warp markers)
 */
class ClipSynchronizer : public ClipManagerListener {
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
     * @brief Handle clip selection changes (no-op)
     * @param clipId The newly selected clip
     */
    void clipSelectionChanged(ClipId clipId) override;

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
    void launchSessionClip(ClipId clipId);

    /**
     * @brief Stop a playing session clip
     * @param clipId The MAGDA clip ID
     *
     * Stops playback and resets synth plugins
     */
    void stopSessionClip(ClipId clipId);

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
     * @brief Get clip ID mapping for external access
     * @return Const reference to clipIdToEngineId_ map
     */
    const std::map<ClipId, std::string>& getClipIdToEngineId() const {
        return clipIdToEngineId_;
    }

  private:
    // =========================================================================
    // Private Sync Helpers
    // =========================================================================

    /**
     * @brief Sync MIDI clip properties to Tracktion Engine
     * @param clipId The MAGDA clip ID
     * @param clip The ClipInfo from ClipManager
     *
     * Handles position, looping, offset, and note data synchronization
     */
    void syncMidiClipToEngine(ClipId clipId, const ClipInfo* clip);

    /**
     * @brief Sync audio clip properties to Tracktion Engine
     * @param clipId The MAGDA clip ID
     * @param clip The ClipInfo from ClipManager
     *
     * Handles position, speed, tempo sync, loop, offset, pitch, fades, etc.
     * Complex logic for beat-based vs. time-based properties
     */
    void syncAudioClipToEngine(ClipId clipId, const ClipInfo* clip);

    // References to dependencies (not owned)
    te::Edit& edit_;
    TrackController& trackController_;
    WarpMarkerManager& warpMarkerManager_;

    // Clip ID mappings
    std::map<ClipId, std::string> clipIdToEngineId_;  // MAGDA → TE
    std::map<std::string, ClipId> engineIdToClipId_;  // TE → MAGDA

    // Reverse proxy state (for deferred reallocation)
    ClipId pendingReverseClipId_{INVALID_CLIP_ID};

    // Thread safety
    mutable juce::CriticalSection clipLock_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipSynchronizer)
};

}  // namespace magda
