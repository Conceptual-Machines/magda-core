#pragma once

#include "selection_api.hpp"

namespace magda {

/// Forwards every SelectionApi call to SelectionManager::getInstance().
class SelectionApiLive : public SelectionApi {
  public:
    TrackId getSelectedTrack() const override;
    ClipId getSelectedClip() const override;
    const std::unordered_set<ClipId>& getSelectedClips() const override;

    AutomationLaneId getSelectedAutomationLaneId() const override;

    bool hasNoteSelection() const override;
    ClipId getNoteSelectionClipId() const override;
    const std::vector<size_t>& getNoteSelectionIndices() const override;

    void selectTrack(TrackId trackId) override;
    void selectTracks(const std::unordered_set<TrackId>& trackIds) override;
    void selectClip(ClipId clipId) override;
    void selectClips(const std::unordered_set<ClipId>& clipIds) override;
    void selectNotes(ClipId clipId, const std::vector<size_t>& noteIndices) override;
    void clearNoteSelection() override;
};

}  // namespace magda
