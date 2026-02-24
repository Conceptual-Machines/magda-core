#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <unordered_map>
#include <vector>

#include "../core/ClipInfo.hpp"
#include "../core/ClipManager.hpp"
#include "../core/ClipTypes.hpp"
#include "../core/TrackTypes.hpp"

namespace magda {

namespace te = tracktion;

/**
 * @brief Records session clip performances into the arrangement timeline.
 *
 * When recording is active and session clips are triggered, their content
 * is written to the arrangement at the current transport position. When a
 * clip stops (or is replaced by another on the same track), the arrangement
 * clip's length matches the played duration.
 */
class SessionRecorder : public ClipManagerListener {
  public:
    explicit SessionRecorder(te::Edit& edit);
    ~SessionRecorder() override;

    void setRecordingActive(bool active);
    bool isRecordingActive() const;

    // ClipManagerListener
    void clipsChanged() override {}
    void clipPropertyChanged(ClipId /*clipId*/) override {}
    void clipSelectionChanged(ClipId /*clipId*/) override {}
    void clipPlaybackStateChanged(ClipId clipId) override;

  private:
    struct ActiveRecording {
        ClipId sessionClipId = INVALID_CLIP_ID;
        TrackId trackId = INVALID_TRACK_ID;
        double arrangementStartTime = 0.0;
    };

    void finalizeRecording(const ActiveRecording& rec, double stopTime);
    void commitAllRecordings();

    te::Edit& edit_;
    bool recordingActive_ = false;
    std::unordered_map<ClipId, ActiveRecording> activeRecordings_;
    std::vector<ClipInfo> arrangementSnapshotBeforeRecord_;
    std::vector<ClipId> createdArrangementClipIds_;
};

}  // namespace magda
