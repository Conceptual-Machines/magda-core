#include "SelectionManager.hpp"

#include <algorithm>

#include "ClipManager.hpp"
#include "TrackManager.hpp"

namespace magda {

SelectionManager& SelectionManager::getInstance() {
    static SelectionManager instance;
    return instance;
}

SelectionManager::SelectionManager() {
    // Start with no selection
}

// ============================================================================
// Track Selection
// ============================================================================

void SelectionManager::selectTrack(TrackId trackId) {
    bool typeChanged = selectionType_ != SelectionType::Track;
    bool trackChanged = selectedTrackId_ != trackId;

    // Clear other selection types
    selectedClipId_ = INVALID_CLIP_ID;
    timeRangeSelection_ = TimeRangeSelection{};

    selectionType_ = SelectionType::Track;
    selectedTrackId_ = trackId;

    // Sync with TrackManager
    TrackManager::getInstance().setSelectedTrack(trackId);

    // Sync with ClipManager (clear clip selection)
    ClipManager::getInstance().clearClipSelection();

    if (typeChanged) {
        notifySelectionTypeChanged(SelectionType::Track);
    }
    if (trackChanged) {
        notifyTrackSelectionChanged(trackId);
    }
}

// ============================================================================
// Clip Selection
// ============================================================================

void SelectionManager::selectClip(ClipId clipId) {
    bool typeChanged = selectionType_ != SelectionType::Clip;
    bool clipChanged = selectedClipId_ != clipId;

    // Clear other selection types
    selectedTrackId_ = INVALID_TRACK_ID;
    selectedClipIds_.clear();
    timeRangeSelection_ = TimeRangeSelection{};

    selectionType_ = SelectionType::Clip;
    selectedClipId_ = clipId;

    // Set this as the anchor for Shift+click range selection
    anchorClipId_ = clipId;

    // Also add to the set for consistency
    if (clipId != INVALID_CLIP_ID) {
        selectedClipIds_.insert(clipId);
    }

    // Sync with ClipManager
    ClipManager::getInstance().setSelectedClip(clipId);

    // Sync with TrackManager (clear track selection)
    TrackManager::getInstance().setSelectedTrack(INVALID_TRACK_ID);

    if (typeChanged) {
        notifySelectionTypeChanged(SelectionType::Clip);
    }
    if (clipChanged) {
        notifyClipSelectionChanged(clipId);
    }
}

// ============================================================================
// Multi-Clip Selection
// ============================================================================

void SelectionManager::selectClips(const std::unordered_set<ClipId>& clipIds) {
    if (clipIds.empty()) {
        clearSelection();
        return;
    }

    if (clipIds.size() == 1) {
        // Single clip - use regular selectClip for backward compat
        selectClip(*clipIds.begin());
        return;
    }

    bool typeChanged = selectionType_ != SelectionType::MultiClip;

    // Clear other selection types
    selectedTrackId_ = INVALID_TRACK_ID;
    selectedClipId_ = INVALID_CLIP_ID;
    timeRangeSelection_ = TimeRangeSelection{};

    selectionType_ = SelectionType::MultiClip;
    selectedClipIds_ = clipIds;

    // Sync with managers (clear single-clip selection)
    ClipManager::getInstance().clearClipSelection();
    TrackManager::getInstance().setSelectedTrack(INVALID_TRACK_ID);

    if (typeChanged) {
        notifySelectionTypeChanged(SelectionType::MultiClip);
    }
    notifyMultiClipSelectionChanged(selectedClipIds_);
}

void SelectionManager::addClipToSelection(ClipId clipId) {
    if (clipId == INVALID_CLIP_ID) {
        return;
    }

    // If currently single-clip selection, convert to multi-clip
    if (selectionType_ == SelectionType::Clip && selectedClipId_ != INVALID_CLIP_ID) {
        selectedClipIds_.insert(selectedClipId_);
    }

    // Add the new clip
    selectedClipIds_.insert(clipId);

    if (selectedClipIds_.size() == 1) {
        // Still just one clip - use single selection mode
        selectClip(clipId);
    } else {
        // Multiple clips - switch to multi-clip mode
        bool typeChanged = selectionType_ != SelectionType::MultiClip;

        selectedTrackId_ = INVALID_TRACK_ID;
        selectedClipId_ = INVALID_CLIP_ID;
        timeRangeSelection_ = TimeRangeSelection{};

        selectionType_ = SelectionType::MultiClip;

        // Sync with managers
        ClipManager::getInstance().clearClipSelection();
        TrackManager::getInstance().setSelectedTrack(INVALID_TRACK_ID);

        if (typeChanged) {
            notifySelectionTypeChanged(SelectionType::MultiClip);
        }
        notifyMultiClipSelectionChanged(selectedClipIds_);
    }
}

void SelectionManager::removeClipFromSelection(ClipId clipId) {
    selectedClipIds_.erase(clipId);

    if (selectedClipIds_.empty()) {
        clearSelection();
    } else if (selectedClipIds_.size() == 1) {
        // Back to single selection
        selectClip(*selectedClipIds_.begin());
    } else {
        // Still multi-clip
        notifyMultiClipSelectionChanged(selectedClipIds_);
    }
}

void SelectionManager::toggleClipSelection(ClipId clipId) {
    if (isClipSelected(clipId)) {
        removeClipFromSelection(clipId);
    } else {
        addClipToSelection(clipId);
    }
}

void SelectionManager::extendSelectionTo(ClipId targetClipId) {
    if (targetClipId == INVALID_CLIP_ID) {
        return;
    }

    // If no anchor, just select the target
    if (anchorClipId_ == INVALID_CLIP_ID) {
        selectClip(targetClipId);
        return;
    }

    // Get anchor and target clip info
    const auto* anchorClip = ClipManager::getInstance().getClip(anchorClipId_);
    const auto* targetClip = ClipManager::getInstance().getClip(targetClipId);

    if (!anchorClip || !targetClip) {
        selectClip(targetClipId);
        return;
    }

    // Calculate the rectangular region between anchor and target
    double minTime = std::min(anchorClip->startTime, targetClip->startTime);
    double maxTime = std::max(anchorClip->startTime + anchorClip->length,
                              targetClip->startTime + targetClip->length);

    TrackId minTrackId = std::min(anchorClip->trackId, targetClip->trackId);
    TrackId maxTrackId = std::max(anchorClip->trackId, targetClip->trackId);

    // Find all clips in this region
    std::unordered_set<ClipId> clipsInRange;
    const auto& allClips = ClipManager::getInstance().getClips();

    for (const auto& clip : allClips) {
        // Check if clip's track is in range
        if (clip.trackId < minTrackId || clip.trackId > maxTrackId) {
            continue;
        }

        // Check if clip overlaps with time range
        double clipEnd = clip.startTime + clip.length;
        if (clip.startTime < maxTime && clipEnd > minTime) {
            clipsInRange.insert(clip.id);
        }
    }

    // Select all clips in range (preserve anchor)
    ClipId savedAnchor = anchorClipId_;
    selectClips(clipsInRange);
    anchorClipId_ = savedAnchor;
}

bool SelectionManager::isClipSelected(ClipId clipId) const {
    if (selectionType_ == SelectionType::Clip) {
        return selectedClipId_ == clipId;
    }
    if (selectionType_ == SelectionType::MultiClip) {
        return selectedClipIds_.find(clipId) != selectedClipIds_.end();
    }
    return false;
}

// ============================================================================
// Time Range Selection
// ============================================================================

void SelectionManager::selectTimeRange(double startTime, double endTime,
                                       const std::vector<TrackId>& trackIds) {
    bool typeChanged = selectionType_ != SelectionType::TimeRange;

    // Clear other selection types
    selectedTrackId_ = INVALID_TRACK_ID;
    selectedClipId_ = INVALID_CLIP_ID;

    selectionType_ = SelectionType::TimeRange;
    timeRangeSelection_.startTime = startTime;
    timeRangeSelection_.endTime = endTime;
    timeRangeSelection_.trackIds = trackIds;

    // Sync with managers (clear their selections)
    TrackManager::getInstance().setSelectedTrack(INVALID_TRACK_ID);
    ClipManager::getInstance().clearClipSelection();

    if (typeChanged) {
        notifySelectionTypeChanged(SelectionType::TimeRange);
    }
    notifyTimeRangeSelectionChanged(timeRangeSelection_);
}

// ============================================================================
// Note Selection
// ============================================================================

void SelectionManager::selectNote(ClipId clipId, size_t noteIndex) {
    bool typeChanged = selectionType_ != SelectionType::Note;

    // Clear other selection types (but keep clip selection for UI purposes)
    selectedTrackId_ = INVALID_TRACK_ID;
    selectedClipId_ = INVALID_CLIP_ID;
    selectedClipIds_.clear();
    timeRangeSelection_ = TimeRangeSelection{};

    selectionType_ = SelectionType::Note;
    noteSelection_.clipId = clipId;
    noteSelection_.noteIndices.clear();
    noteSelection_.noteIndices.push_back(noteIndex);

    // Clear track selection but DON'T clear clip selection
    // (the note is still within that clip, and we want the piano roll to stay visible)
    TrackManager::getInstance().setSelectedTrack(INVALID_TRACK_ID);

    if (typeChanged) {
        notifySelectionTypeChanged(SelectionType::Note);
    }
    notifyNoteSelectionChanged(noteSelection_);
}

void SelectionManager::selectNotes(ClipId clipId, const std::vector<size_t>& noteIndices) {
    if (noteIndices.empty()) {
        clearSelection();
        return;
    }

    if (noteIndices.size() == 1) {
        selectNote(clipId, noteIndices[0]);
        return;
    }

    bool typeChanged = selectionType_ != SelectionType::Note;

    // Clear other selection types (but keep clip selection for UI purposes)
    selectedTrackId_ = INVALID_TRACK_ID;
    selectedClipId_ = INVALID_CLIP_ID;
    selectedClipIds_.clear();
    timeRangeSelection_ = TimeRangeSelection{};

    selectionType_ = SelectionType::Note;
    noteSelection_.clipId = clipId;
    noteSelection_.noteIndices = noteIndices;

    // Clear track selection but DON'T clear clip selection
    TrackManager::getInstance().setSelectedTrack(INVALID_TRACK_ID);

    if (typeChanged) {
        notifySelectionTypeChanged(SelectionType::Note);
    }
    notifyNoteSelectionChanged(noteSelection_);
}

void SelectionManager::addNoteToSelection(ClipId clipId, size_t noteIndex) {
    // If selecting a note from a different clip, start fresh
    if (noteSelection_.clipId != clipId) {
        selectNote(clipId, noteIndex);
        return;
    }

    // Check if already selected
    auto it =
        std::find(noteSelection_.noteIndices.begin(), noteSelection_.noteIndices.end(), noteIndex);
    if (it != noteSelection_.noteIndices.end()) {
        return;  // Already selected
    }

    // Ensure we're in note selection mode
    if (selectionType_ != SelectionType::Note) {
        selectNote(clipId, noteIndex);
        return;
    }

    noteSelection_.noteIndices.push_back(noteIndex);
    notifyNoteSelectionChanged(noteSelection_);
}

void SelectionManager::removeNoteFromSelection(size_t noteIndex) {
    auto it =
        std::find(noteSelection_.noteIndices.begin(), noteSelection_.noteIndices.end(), noteIndex);
    if (it != noteSelection_.noteIndices.end()) {
        noteSelection_.noteIndices.erase(it);

        if (noteSelection_.noteIndices.empty()) {
            clearSelection();
        } else {
            notifyNoteSelectionChanged(noteSelection_);
        }
    }
}

void SelectionManager::toggleNoteSelection(ClipId clipId, size_t noteIndex) {
    if (isNoteSelected(clipId, noteIndex)) {
        removeNoteFromSelection(noteIndex);
    } else {
        addNoteToSelection(clipId, noteIndex);
    }
}

bool SelectionManager::isNoteSelected(ClipId clipId, size_t noteIndex) const {
    if (selectionType_ != SelectionType::Note || noteSelection_.clipId != clipId) {
        return false;
    }
    return std::find(noteSelection_.noteIndices.begin(), noteSelection_.noteIndices.end(),
                     noteIndex) != noteSelection_.noteIndices.end();
}

// ============================================================================
// Clear
// ============================================================================

void SelectionManager::clearSelection() {
    if (selectionType_ == SelectionType::None) {
        return;
    }

    selectionType_ = SelectionType::None;
    selectedTrackId_ = INVALID_TRACK_ID;
    selectedClipId_ = INVALID_CLIP_ID;
    anchorClipId_ = INVALID_CLIP_ID;
    selectedClipIds_.clear();
    timeRangeSelection_ = TimeRangeSelection{};
    noteSelection_ = NoteSelection{};

    // Sync with managers
    TrackManager::getInstance().setSelectedTrack(INVALID_TRACK_ID);
    ClipManager::getInstance().clearClipSelection();

    notifySelectionTypeChanged(SelectionType::None);
}

// ============================================================================
// Listeners
// ============================================================================

void SelectionManager::addListener(SelectionManagerListener* listener) {
    if (listener && std::find(listeners_.begin(), listeners_.end(), listener) == listeners_.end()) {
        listeners_.push_back(listener);
    }
}

void SelectionManager::removeListener(SelectionManagerListener* listener) {
    listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), listener), listeners_.end());
}

// ============================================================================
// Private Notification Helpers
// ============================================================================

void SelectionManager::notifySelectionTypeChanged(SelectionType type) {
    for (auto* listener : listeners_) {
        listener->selectionTypeChanged(type);
    }
}

void SelectionManager::notifyTrackSelectionChanged(TrackId trackId) {
    for (auto* listener : listeners_) {
        listener->trackSelectionChanged(trackId);
    }
}

void SelectionManager::notifyClipSelectionChanged(ClipId clipId) {
    for (auto* listener : listeners_) {
        listener->clipSelectionChanged(clipId);
    }
}

void SelectionManager::notifyMultiClipSelectionChanged(const std::unordered_set<ClipId>& clipIds) {
    for (auto* listener : listeners_) {
        listener->multiClipSelectionChanged(clipIds);
    }
}

void SelectionManager::notifyTimeRangeSelectionChanged(const TimeRangeSelection& selection) {
    for (auto* listener : listeners_) {
        listener->timeRangeSelectionChanged(selection);
    }
}

void SelectionManager::notifyNoteSelectionChanged(const NoteSelection& selection) {
    for (auto* listener : listeners_) {
        listener->noteSelectionChanged(selection);
    }
}

}  // namespace magda
