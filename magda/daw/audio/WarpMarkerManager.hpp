#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
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
 * @brief Transient detection for audio clips, cached per source file
 *
 * Detection runs through Tracktion Engine's WarpTimeManager. The marker map
 * itself is the model's (#2760); the clip sync mirrors it onto the fork.
 *
 * Thread Safety:
 * - All operations run on message thread (UI thread)
 * - Delegates to Tracktion Engine's WarpTimeManager
 */
class WarpMarkerManager : private te::WarpTimeManager::Listener, private juce::Timer {
  public:
    WarpMarkerManager() = default;
    ~WarpMarkerManager() override;

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

  private:
    // -------- In-flight guard for transient detection --------
    //
    // The UI can spam sensitivity changes (slider drag = many calls/sec).
    // Retain only the latest value and start one detection after the gesture
    // settles, keeping the current overlay stable in the meantime. All clips
    // share this quiet-period timer: an update for any clip restarts it, then
    // every pending clip is processed together once input settles.
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
    void timerCallback() override;

    std::map<ClipId, PendingDetection> pendingDetections_;
    std::set<ClipId> detectionInFlight_;
    std::map<ClipId, ActiveDetection> activeDetections_;
    std::map<te::WarpTimeManager*, ClipId> clipByWarpManager_;

    JUCE_DECLARE_WEAK_REFERENCEABLE(WarpMarkerManager)
};

}  // namespace magda
