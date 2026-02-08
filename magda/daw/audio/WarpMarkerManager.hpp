#pragma once

#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <map>
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
class WarpMarkerManager {
  public:
    WarpMarkerManager() = default;
    ~WarpMarkerManager() = default;

    /**
     * @brief Detect transient times for an audio clip's source file
     *
     * On first call, kicks off async transient detection via TE's WarpTimeManager.
     * Subsequent calls poll for completion. Results are cached per file path.
     *
     * @param edit Tracktion Engine edit
     * @param clipIdToEngineId Mapping from MAGDA clip ID to TE clip ID
     * @param clipId The MAGDA clip ID (must be an audio clip)
     * @return true if transients are ready (cached), false if still detecting
     */
    bool getTransientTimes(te::Edit& edit, const std::map<ClipId, std::string>& clipIdToEngineId,
                           ClipId clipId);

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
};

}  // namespace magda
