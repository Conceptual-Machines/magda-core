#pragma once

#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "../core/ClipTypes.hpp"
#include "../core/TypeIds.hpp"

namespace magda {

// Forward declarations
namespace te = tracktion;

/**
 * @brief Warp marker information for UI display
 */
struct WarpMarkerInfo {
    double sourceTime;
    double warpTime;
};

/**
 * @brief Manages warp markers and transient detection for audio clips
 *
 * Responsibilities:
 * - Transient detection (async via Tracktion Engine's WarpTimeManager)
 * - Warp marker enable/disable
 * - Warp marker CRUD operations (add, move, remove, get)
 * - Caching of transient times
 *
 * Thread Safety:
 * - All operations run on message thread (UI thread)
 * - Delegates to Tracktion Engine's WarpTimeManager
 */
class WarpMarkerManager : private te::WarpTimeManager::Listener {
  public:
    WarpMarkerManager() = default;
    ~WarpMarkerManager();

    /**
     * @brief Detect transient times for an audio clip's source file
     *
     * On first call, kicks off async transient detection via TE's WarpTimeManager.
     * Completion is delivered by WarpTimeManager callback and cached per file path.
     *
     * @param edit Tracktion Engine edit
     * @param clipIdToEngineId Mapping from MAGDA clip ID to TE clip ID
     * @param clipId The MAGDA clip ID (must be an audio clip)
     * @return true if transients are ready (cached), false if still detecting
     */
    bool getTransientTimes(te::Edit& edit, const std::map<ClipId, std::string>& clipIdToEngineId,
                           ClipId clipId);

    /**
     * @brief Set transient detection sensitivity and re-run detection
     * @param edit Tracktion Engine edit
     * @param clipIdToEngineId Mapping from MAGDA clip ID to TE clip ID
     * @param clipId The MAGDA clip ID
     * @param sensitivity Sensitivity value (0.0 to 1.0)
     */
    void setTransientSensitivity(te::Edit& edit,
                                 const std::map<ClipId, std::string>& clipIdToEngineId,
                                 ClipId clipId, float sensitivity);

    /**
     * @brief Enable warping: populate WarpTimeManager with markers at detected transients
     * @param edit Tracktion Engine edit
     * @param clipIdToEngineId Mapping from MAGDA clip ID to TE clip ID
     * @param clipId The MAGDA clip ID
     */
    void enableWarp(te::Edit& edit, const std::map<ClipId, std::string>& clipIdToEngineId,
                    ClipId clipId);

    /**
     * @brief Disable warping: remove all warp markers
     * @param edit Tracktion Engine edit
     * @param clipIdToEngineId Mapping from MAGDA clip ID to TE clip ID
     * @param clipId The MAGDA clip ID
     */
    void disableWarp(te::Edit& edit, const std::map<ClipId, std::string>& clipIdToEngineId,
                     ClipId clipId);

    /**
     * @brief Get current warp marker positions for display
     * @param edit Tracktion Engine edit
     * @param clipIdToEngineId Mapping from MAGDA clip ID to TE clip ID
     * @param clipId The MAGDA clip ID
     * @return Vector of warp marker info
     */
    std::vector<WarpMarkerInfo> getWarpMarkers(
        te::Edit& edit, const std::map<ClipId, std::string>& clipIdToEngineId, ClipId clipId);

    /**
     * @brief Add a warp marker
     * @param edit Tracktion Engine edit
     * @param clipIdToEngineId Mapping from MAGDA clip ID to TE clip ID
     * @param clipId The MAGDA clip ID
     * @param sourceTime Source time position
     * @param warpTime Warped time position
     * @return Index of inserted marker, or -1 on failure
     */
    int addWarpMarker(te::Edit& edit, const std::map<ClipId, std::string>& clipIdToEngineId,
                      ClipId clipId, double sourceTime, double warpTime);

    /**
     * @brief Move a warp marker's warp time
     * @param edit Tracktion Engine edit
     * @param clipIdToEngineId Mapping from MAGDA clip ID to TE clip ID
     * @param clipId The MAGDA clip ID
     * @param index Marker index
     * @param newWarpTime New warped time position
     * @return Actual position (clamped by TE)
     */
    double moveWarpMarker(te::Edit& edit, const std::map<ClipId, std::string>& clipIdToEngineId,
                          ClipId clipId, int index, double newWarpTime);

    /**
     * @brief Remove a warp marker at index
     * @param edit Tracktion Engine edit
     * @param clipIdToEngineId Mapping from MAGDA clip ID to TE clip ID
     * @param clipId The MAGDA clip ID
     * @param index Marker index
     */
    void removeWarpMarker(te::Edit& edit, const std::map<ClipId, std::string>& clipIdToEngineId,
                          ClipId clipId, int index);

  private:
    // -------- In-flight guard for transient detection --------
    //
    // The UI can spam sensitivity changes (slider drag = many calls/sec).
    // Restart detection immediately for the latest value; WarpTimeManager
    // detaches/cancels the previous job and reports the surviving job by
    // callback, so stale jobs can't overwrite the cache.
    struct PendingDetection {
        float sensitivity = 0.0f;
        // Resolved at queue time so the slider hot-path doesn't snapshot the
        // whole clipIdToEngineId map per tick. Empty engineId means we
        // couldn't resolve the clip (e.g. it was removed) — apply will skip.
        std::string engineId;
        te::Edit* edit = nullptr;
    };

    struct ActiveDetection {
        juce::String filePath;
        te::WarpTimeManager::Ptr warpManager;
    };

    bool startDetection(te::Edit& edit, const std::string& engineId, ClipId clipId,
                        std::optional<float> sensitivity);
    void applySensitivityNow(te::Edit& edit, const std::string& engineId, ClipId clipId,
                             float sensitivity);
    void finishDetection(ClipId clipId, te::WarpTimeManager& warpManager, bool completedOk);
    void transientDetectionFinished(te::WarpTimeManager&, bool completedOk) override;

    std::set<ClipId> detectionInFlight_;
    std::map<ClipId, ActiveDetection> activeDetections_;
    std::map<te::WarpTimeManager*, ClipId> clipByWarpManager_;
};

}  // namespace magda
