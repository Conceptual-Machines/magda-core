#pragma once

#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
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
 * - Bidirectional clip ID mapping (ClipId <-> TE EditItemID)
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
    ClipSynchronizer(te::Edit& edit, TrackController& trackController,
                     WarpMarkerManager& warpMarkerManager);

    /** @brief Unregisters from ClipManager listener. */
    ~ClipSynchronizer() override;

    // =========================================================================
    // ClipManagerListener Interface
    // =========================================================================

    /**
     * @brief Handle clip additions, deletions, and reordering.
     *
     * Removes clips no longer in ClipManager, then syncs all arrangement
     * clips to TE and all session clips to slots.
     */
    void clipsChanged() override;

    /** @brief Route a single clip's property change to the right sync method. */
    void clipPropertyChanged(ClipId clipId) override;

    /**
     * @brief Handle batched clip property changes synchronously.
     *
     * Deduplicates IDs and coalesces playback graph reallocation while still
     * leaving TE state up to date before returning.
     */
    void clipPropertiesChanged(const std::vector<ClipId>& clipIds) override;

    /** @brief No-op: clip selection does not need to sync to TE. */
    void clipSelectionChanged(ClipId clipId) override;

    // =========================================================================
    // TrackManagerListener Interface
    // =========================================================================

    void tracksChanged() override {}
    void trackPropertyChanged(int trackId) override;

    // =========================================================================
    // Arrangement Clip Operations
    // =========================================================================

    /** @brief Route to syncMidiClipToEngine() or syncAudioClipToEngine() based on type. */
    void syncClipToEngine(ClipId clipId);

    /** @brief Remove a clip from TE and clear its bidirectional mapping. */
    void removeClipFromEngine(ClipId clipId);

    /** @brief The TE Clip for this arrangement clip, or nullptr if not found. */
    te::Clip* getArrangementTeClip(ClipId clipId) const;

    // =========================================================================
    // Session Clip Operations
    // =========================================================================

    /**
     * @brief Sync a session clip to its slot in TE Edit.
     * @return true if a new clip was created (requires graph reallocation).
     */
    bool syncSessionClipToSlot(ClipId clipId);

    /** @brief Remove a session clip from its slot. */
    void removeSessionClipFromSlot(ClipId clipId);

    /** @brief Configure looping and launch a session clip via LaunchHandle. */
    void launchSessionClip(ClipId clipId, bool forceImmediate = false);

    /** @brief Stop a playing session clip immediately. */
    void stopSessionClip(ClipId clipId);

    /** @brief Stop a playing session clip at the next quantization grid point. */
    void stopSessionClipQueued(ClipId clipId, LaunchQuantize quantize);

    /** @brief The TE Clip in the session slot, or nullptr if not found. */
    te::Clip* getSessionTeClip(ClipId clipId);

    // =========================================================================
    // Warp Marker Operations (Delegated to WarpMarkerManager)
    // =========================================================================

    /** @brief Set transient detection sensitivity and re-run detection. */
    void setTransientSensitivity(ClipId clipId, float sensitivity);

    /** @return true if transients were found. */
    bool getTransientTimes(ClipId clipId);

    /** @brief Enable warp/time-stretch for a clip. */
    void enableWarp(ClipId clipId);

    /** @brief Disable warp/time-stretch for a clip. */
    void disableWarp(ClipId clipId);

    /** @brief All warp markers for a clip. */
    std::vector<WarpMarkerInfo> getWarpMarkers(ClipId clipId);

    /** @brief Add a warp marker; returns the index of the added marker. */
    int addWarpMarker(ClipId clipId, double sourceTime, double warpTime);

    /** @return the actual new warp time, which may be clamped. */
    double moveWarpMarker(ClipId clipId, int markerIndex, double newWarpTime);

    /** @brief Remove a warp marker from a clip. */
    void removeWarpMarker(ClipId clipId, int markerIndex);

    // =========================================================================
    // Utilities
    // =========================================================================

    /**
     * @brief Callback fired after the playback graph is reallocated.
     * Used by AudioBridge to re-establish MIDI routing and input monitor state.
     */
    std::function<void()> onGraphReallocated;

    /** @brief The clip a reverse proxy operation is pending for, polled by AudioBridge's timer. */
    ClipId getPendingReverseClipId() const {
        return pendingReverseClipId_;
    }

    /** @brief Clear pending reverse clip ID after proxy completion. */
    void clearPendingReverseClipId() {
        pendingReverseClipId_ = INVALID_CLIP_ID;
    }

    /** @return the precise quantized launch time for a track's last-launched session clip, or 0.0.
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

    /** @return true if this sync changed topology, requiring graph reallocation. */
    bool syncClipPropertyToEngine(ClipId clipId);

    /** @brief Sync arrangement clip data; returns whether graph reallocation is needed. */
    bool syncArrangementClipToEngine(ClipId clipId);

    /** @brief Sync MIDI clip position, looping, offset, and note data to TE. */
    bool syncMidiClipToEngine(ClipId clipId, const ClipInfo* clip);

    /**
     * @brief Sync audio clip position, speed, tempo sync, loop, offset, pitch,
     * and fades to TE. Beat-based and time-based properties need separate
     * handling (see docs/coordinate-system).
     */
    bool syncAudioClipToEngine(ClipId clipId, const ClipInfo* clip);

    /**
     * @brief Re-attach loop-record takes onto a freshly built TE clip.
     *
     * The model owns the take list; the TE clip is rebuilt from scratch on
     * record and on project load, so takes must be re-added each time.
     * Idempotent. The active take plays because the clip's source already
     * points at takes[currentTakeIndex]; the rest are kept as alternates.
     */
    void applyModelTakesToTeClip(tracktion::WaveAudioClip& teClip, const ClipInfo& clip);

    /**
     * @brief Configure autoTempo on a session audio clip in TE.
     *
     * Shared by syncSessionClipToSlot() and clipPropertyChanged(). Syncs
     * source interpretation BPM, stretch mode, speedRatio, autoTempo flag,
     * offset, and beat-based loop range.
     */
    void configureSessionAutoTempo(te::WaveAudioClip* audioClip, const ClipInfo* clip);

    /** @brief Sync TrackInfo::playbackMode to TE. The single place that writes
     * audioTrack->playSlotClips. */
    void syncPlaybackModeToEngine(TrackId trackId);

    /** @brief Remove a TE clip by its engine ID from any track. */
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
