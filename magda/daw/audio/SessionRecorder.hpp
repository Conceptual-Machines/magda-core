#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <functional>
#include <unordered_map>
#include <vector>

#include "../core/ClipInfo.hpp"
#include "../core/ClipManager.hpp"
#include "../core/ClipTypes.hpp"
#include "../core/TrackTypes.hpp"
#include "RecordingNoteQueue.hpp"

namespace magda {

namespace te = tracktion;

/**
 * @brief Records session clip performances into the arrangement.
 *
 * Lives for the app's lifetime. When armed (user pressed Record on transport),
 * captures session clip play/stop events and writes arrangement clips.
 * Recording only begins when a session clip actually starts playing,
 * so there's no gap between pressing Record and triggering a clip.
 */
class SessionRecorder : public ClipManagerListener {
  public:
    explicit SessionRecorder(te::Edit& edit);
    ~SessionRecorder() override;

    /** Arm/disarm session recording. Set by transport Record button. */
    void setArmed(bool armed);
    bool isArmed() const {
        return armed_;
    }

    /** Set the recording previews map (owned by TracktionEngineWrapper). */
    void setRecordingPreviews(std::unordered_map<TrackId, RecordingPreview>* previews) {
        recordingPreviews_ = previews;
    }

    /** Set the play state query function (delegates to SessionClipScheduler). */
    void setPlayStateQuery(std::function<SessionClipPlayState(ClipId)> fn) {
        getPlayState_ = std::move(fn);
    }

    /** Set the launch time query function (delegates to ClipSynchronizer via AudioBridge). */
    void setLaunchTimeQuery(std::function<double(TrackId)> fn) {
        getLaunchTime_ = std::move(fn);
    }

    /** Update preview lengths to match current transport position. Call each frame. */
    void updatePreviews();

    /**
     * @brief Finalize active recordings and push undo command.
     * Called on transport stop.
     */
    void commitIfNeeded();

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

    void ensureSnapshotTaken();
    void finalizeRecording(const ActiveRecording& rec, double stopTime);

    te::Edit& edit_;
    bool armed_ = false;
    bool snapshotTaken_ = false;
    std::unordered_map<ClipId, ActiveRecording> activeRecordings_;
    std::vector<ClipInfo> arrangementSnapshotBeforeRecord_;
    std::vector<ClipId> createdArrangementClipIds_;
    std::unordered_map<TrackId, RecordingPreview>* recordingPreviews_ = nullptr;
    std::function<SessionClipPlayState(ClipId)> getPlayState_;
    std::function<double(TrackId)> getLaunchTime_;
};

}  // namespace magda
