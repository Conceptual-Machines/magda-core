// TrackContentPanel — Multi-clip drag, time selection clip handling, and clip ghost methods.
// Split from TrackContentPanel.cpp for file-size compliance.

#include "../clips/ClipComponent.hpp"
#include "TrackContentPanel.hpp"
#include "core/ClipCommands.hpp"
#include "core/SelectionManager.hpp"
#include "core/UndoManager.hpp"

namespace magda {

// ============================================================================
// Clip Creation & Lookup
// ============================================================================

void TrackContentPanel::createClipFromTimeSelection() {
    if (!timelineController) {
        return;
    }

    const auto& selection = timelineController->getState().selection;
    if (!selection.isActive()) {
        return;
    }

    // Count how many clips will be created
    int clipCount = 0;
    for (int trackIndex : selection.trackIndices) {
        if (trackIndex >= 0 && trackIndex < static_cast<int>(visibleTrackIds_.size())) {
            clipCount++;
        }
    }

    // Use compound operation if creating multiple clips
    if (clipCount > 1) {
        UndoManager::getInstance().beginCompoundOperation("Create Clips");
    }

    // Create a clip for each track in the selection through the undo system
    ClipId lastCreatedClip = INVALID_CLIP_ID;
    for (int trackIndex : selection.trackIndices) {
        if (trackIndex >= 0 && trackIndex < static_cast<int>(visibleTrackIds_.size())) {
            TrackId trackId = visibleTrackIds_[trackIndex];
            const auto* track = TrackManager::getInstance().getTrack(trackId);

            if (track) {
                double length = selection.endTime - selection.startTime;

                // Create MIDI clip by default (tracks are hybrid - can contain both MIDI and audio)
                auto cmd = std::make_unique<CreateClipCommand>(ClipType::MIDI, trackId,
                                                               selection.startTime, length);
                UndoManager::getInstance().executeCommand(std::move(cmd));

                // Find the newly created clip to select it
                auto clipId =
                    ClipManager::getInstance().getClipAtPosition(trackId, selection.startTime);
                if (clipId != INVALID_CLIP_ID)
                    lastCreatedClip = clipId;
            }
        }
    }

    if (clipCount > 1) {
        UndoManager::getInstance().endCompoundOperation();
    }

    // Auto-select the last created clip so the editor opens immediately
    if (lastCreatedClip != INVALID_CLIP_ID) {
        SelectionManager::getInstance().selectClip(lastCreatedClip);
    }
}

ClipComponent* TrackContentPanel::getClipComponentAt(int x, int y) const {
    for (const auto& clipComp : clipComponents_) {
        if (clipComp->getBounds().contains(x, y)) {
            return clipComp.get();
        }
    }
    return nullptr;
}

// ============================================================================
// Multi-Clip Drag
// ============================================================================

void TrackContentPanel::startMultiClipDrag(ClipId anchorClipId, const juce::Point<int>& startPos) {
    auto& selectionManager = SelectionManager::getInstance();
    const auto& selectedClips = selectionManager.getSelectedClips();

    if (selectedClips.empty()) {
        return;
    }

    isMovingMultipleClips_ = true;
    anchorClipId_ = anchorClipId;
    multiClipDragStartPos_ = startPos;

    // Get the anchor clip's start time
    const auto* anchorClip = ClipManager::getInstance().getClip(anchorClipId);
    if (anchorClip) {
        multiClipDragStartTime_ = anchorClip->startTime;
    }

    // Store original positions of all selected clips
    multiClipDragInfos_.clear();
    for (ClipId clipId : selectedClips) {
        const auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip) {
            ClipDragInfo info;
            info.clipId = clipId;
            info.originalStartTime = clip->startTime;
            info.originalTrackId = clip->trackId;

            // Find track index
            auto it = std::find(visibleTrackIds_.begin(), visibleTrackIds_.end(), clip->trackId);
            if (it != visibleTrackIds_.end()) {
                info.originalTrackIndex =
                    static_cast<int>(std::distance(visibleTrackIds_.begin(), it));
            }

            multiClipDragInfos_.push_back(info);
        }
    }
}

void TrackContentPanel::updateMultiClipDrag(const juce::Point<int>& currentPos) {
    if (!isMovingMultipleClips_ || multiClipDragInfos_.empty()) {
        return;
    }

    // Check for Alt+drag to duplicate (mark for duplication, created in finishMultiClipDrag)
    bool altHeld = juce::ModifierKeys::getCurrentModifiers().isAltDown();
    if (altHeld && !isMultiClipDuplicating_) {
        isMultiClipDuplicating_ = true;
    }

    // currentZoom is ppb - convert pixel delta to time through beats
    if (currentZoom <= 0 || tempoBPM <= 0) {
        return;
    }

    int deltaX = currentPos.x - multiClipDragStartPos_.x;
    double deltaBeats = deltaX / currentZoom;
    double deltaTime = deltaBeats * 60.0 / tempoBPM;

    // Calculate new anchor time with snapping
    double newAnchorTime = juce::jmax(0.0, multiClipDragStartTime_ + deltaTime);
    if (snapTimeToGrid) {
        double snappedTime = snapTimeToGrid(newAnchorTime);
        // Magnetic snap threshold (convert time diff to pixels through beats)
        double snapDeltaBeats = std::abs((snappedTime - newAnchorTime) * tempoBPM / 60.0);
        double snapDeltaPixels = snapDeltaBeats * currentZoom;
        if (snapDeltaPixels <= 15) {  // SNAP_THRESHOLD_PIXELS
            newAnchorTime = snappedTime;
        }
    }

    double actualDeltaTime = newAnchorTime - multiClipDragStartTime_;

    if (isMultiClipDuplicating_) {
        // Alt+drag duplicate: show ghosts at NEW positions, keep originals in place
        for (const auto& dragInfo : multiClipDragInfos_) {
            double newStartTime = juce::jmax(0.0, dragInfo.originalStartTime + actualDeltaTime);

            const auto* clip = ClipManager::getInstance().getClip(dragInfo.clipId);
            if (clip) {
                // Find the clip component to get its Y position
                for (const auto& clipComp : clipComponents_) {
                    if (clipComp->getClipId() == dragInfo.clipId) {
                        int ghostX = timeToPixel(newStartTime);
                        double ghostBeats = (clip->autoTempo && clip->lengthBeats > 0.0)
                                                ? clip->lengthBeats
                                                : clip->length * tempoBPM / 60.0;
                        int ghostWidth = static_cast<int>(ghostBeats * currentZoom);
                        juce::Rectangle<int> ghostBounds(ghostX, clipComp->getY(),
                                                         juce::jmax(10, ghostWidth),
                                                         clipComp->getHeight());
                        setClipGhost(dragInfo.clipId, ghostBounds, clip->colour);
                        break;
                    }
                }
            }
        }
        // Don't move the original clip components
    } else {
        // Normal move: update all clip component positions visually
        for (const auto& dragInfo : multiClipDragInfos_) {
            double newStartTime = juce::jmax(0.0, dragInfo.originalStartTime + actualDeltaTime);

            // Find the clip component
            for (auto& clipComp : clipComponents_) {
                if (clipComp->getClipId() == dragInfo.clipId) {
                    const auto* clip = ClipManager::getInstance().getClip(dragInfo.clipId);
                    if (clip) {
                        int newX = timeToPixel(newStartTime);
                        double clipBeats = (clip->autoTempo && clip->lengthBeats > 0.0)
                                               ? clip->lengthBeats
                                               : clip->length * tempoBPM / 60.0;
                        int clipWidth = static_cast<int>(clipBeats * currentZoom);
                        clipComp->setBounds(newX, clipComp->getY(), juce::jmax(10, clipWidth),
                                            clipComp->getHeight());
                    }
                    break;
                }
            }
        }
    }
}

void TrackContentPanel::finishMultiClipDrag() {
    if (!isMovingMultipleClips_ || multiClipDragInfos_.empty()) {
        isMovingMultipleClips_ = false;
        return;
    }

    // Clear all ghosts before committing
    clearAllClipGhosts();

    // Get the final anchor position
    ClipComponent* anchorComp = nullptr;
    for (auto& clipComp : clipComponents_) {
        if (clipComp->getClipId() == anchorClipId_) {
            anchorComp = clipComp.get();
            break;
        }
    }

    if (anchorComp) {
        // Calculate final delta from anchor's visual position
        double finalAnchorTime = pixelToTime(anchorComp->getX());
        if (snapTimeToGrid) {
            finalAnchorTime = snapTimeToGrid(finalAnchorTime);
        }
        finalAnchorTime = juce::jmax(0.0, finalAnchorTime);

        double actualDeltaTime = finalAnchorTime - multiClipDragStartTime_;

        if (isMultiClipDuplicating_) {
            // Alt+drag duplicate: create duplicates at final positions through undo system
            if (multiClipDragInfos_.size() > 1) {
                UndoManager::getInstance().beginCompoundOperation("Duplicate Clips");
            }

            std::vector<std::unique_ptr<DuplicateClipCommand>> commands;
            for (const auto& dragInfo : multiClipDragInfos_) {
                double newStartTime = juce::jmax(0.0, dragInfo.originalStartTime + actualDeltaTime);
                auto cmd = std::make_unique<DuplicateClipCommand>(dragInfo.clipId, newStartTime,
                                                                  dragInfo.originalTrackId);
                commands.push_back(std::move(cmd));
            }

            std::unordered_set<ClipId> newClipIds;
            for (auto& cmd : commands) {
                DuplicateClipCommand* cmdPtr = cmd.get();
                UndoManager::getInstance().executeCommand(std::move(cmd));
                ClipId dupId = cmdPtr->getDuplicatedClipId();
                if (dupId != INVALID_CLIP_ID) {
                    newClipIds.insert(dupId);
                }
            }

            if (multiClipDragInfos_.size() > 1) {
                UndoManager::getInstance().endCompoundOperation();
            }

            // Select the duplicates
            if (!newClipIds.empty()) {
                SelectionManager::getInstance().selectClips(newClipIds);
            }
        } else {
            // Normal move: apply to original clips through undo system
            if (multiClipDragInfos_.size() > 1) {
                UndoManager::getInstance().beginCompoundOperation("Move Clips");
            }

            for (const auto& dragInfo : multiClipDragInfos_) {
                double newStartTime = juce::jmax(0.0, dragInfo.originalStartTime + actualDeltaTime);
                auto cmd = std::make_unique<MoveClipCommand>(dragInfo.clipId, newStartTime);
                UndoManager::getInstance().executeCommand(std::move(cmd));
            }

            if (multiClipDragInfos_.size() > 1) {
                UndoManager::getInstance().endCompoundOperation();
            }
        }
    }

    // Clean up
    isMovingMultipleClips_ = false;
    isMultiClipDuplicating_ = false;
    anchorClipId_ = INVALID_CLIP_ID;
    multiClipDragInfos_.clear();
    multiClipDuplicateIds_.clear();

    // Refresh positions from ClipManager
    updateClipComponentPositions();
}

void TrackContentPanel::cancelMultiClipDrag() {
    if (!isMovingMultipleClips_) {
        return;
    }

    // Clear any ghosts that were shown
    clearAllClipGhosts();

    // Restore original visual positions
    updateClipComponentPositions();

    isMovingMultipleClips_ = false;
    isMultiClipDuplicating_ = false;
    anchorClipId_ = INVALID_CLIP_ID;
    multiClipDragInfos_.clear();
    multiClipDuplicateIds_.clear();
}

// ============================================================================
// Time Selection with Clips
// ============================================================================

void TrackContentPanel::splitClipsAtSelectionBoundaries() {
    if (!timelineController)
        return;

    const auto& selection = timelineController->getState().selection;
    if (!selection.isActive())
        return;

    double start = selection.startTime;
    double end = selection.endTime;

    const auto& clips = ClipManager::getInstance().getArrangementClips();

    // For each clip, determine if it needs splitting at left, right, or both boundaries
    struct SplitInfo {
        ClipId clipId;
        bool needsLeftSplit;
        bool needsRightSplit;
    };
    std::vector<SplitInfo> clipsToSplit;

    for (const auto& clip : clips) {
        auto it = std::find(visibleTrackIds_.begin(), visibleTrackIds_.end(), clip.trackId);
        if (it == visibleTrackIds_.end())
            continue;

        int trackIndex = static_cast<int>(std::distance(visibleTrackIds_.begin(), it));
        if (!selection.includesTrack(trackIndex))
            continue;

        double clipEnd = clip.startTime + clip.length;
        bool needsLeft = (clip.startTime < start && clipEnd > start);
        bool needsRight = (clip.startTime < end && clipEnd > end);

        if (needsLeft || needsRight) {
            clipsToSplit.push_back({clip.id, needsLeft, needsRight});
        }
    }

    if (clipsToSplit.empty())
        return;

    int totalOps = 0;
    for (const auto& s : clipsToSplit)
        totalOps += (s.needsLeftSplit ? 1 : 0) + (s.needsRightSplit ? 1 : 0);

    if (totalOps > 1)
        UndoManager::getInstance().beginCompoundOperation("Split Clips at Selection");

    for (const auto& info : clipsToSplit) {
        ClipId rightSideId = info.clipId;

        // Split at left boundary first — the right piece gets a new ID
        if (info.needsLeftSplit) {
            auto cmd = std::make_unique<SplitClipCommand>(info.clipId, start, getTempo());
            auto* cmdPtr = cmd.get();
            UndoManager::getInstance().executeCommand(std::move(cmd));
            // The right piece (from start onward) is the one that may need a right split
            rightSideId = cmdPtr->getRightClipId();
        }

        // Split at right boundary — use the right piece from the left split if applicable
        if (info.needsRightSplit) {
            const auto* clip = ClipManager::getInstance().getClip(rightSideId);
            if (clip && end > clip->startTime && end < clip->startTime + clip->length) {
                auto cmd = std::make_unique<SplitClipCommand>(rightSideId, end, getTempo());
                UndoManager::getInstance().executeCommand(std::move(cmd));
            }
        }
    }

    if (totalOps > 1)
        UndoManager::getInstance().endCompoundOperation();
}

void TrackContentPanel::captureClipsInTimeSelection() {
    clipsInTimeSelection_.clear();

    if (!timelineController) {
        return;
    }

    const auto& selection = timelineController->getState().selection;
    if (!selection.isActive()) {
        return;
    }

    // Get only arrangement clips and check if they overlap with the time selection
    const auto& clips = ClipManager::getInstance().getArrangementClips();

    for (const auto& clip : clips) {
        // Check if clip's track is in the selection
        auto it = std::find(visibleTrackIds_.begin(), visibleTrackIds_.end(), clip.trackId);
        if (it == visibleTrackIds_.end()) {
            continue;  // Track not visible
        }

        int trackIndex = static_cast<int>(std::distance(visibleTrackIds_.begin(), it));
        if (!selection.includesTrack(trackIndex)) {
            continue;  // Track not in selection
        }

        // Check if clip overlaps with selection time range
        double clipEnd = clip.startTime + clip.length;
        if (clip.startTime < selection.endTime && clipEnd > selection.startTime) {
            // Clip overlaps with selection - capture it
            TimeSelectionClipInfo info;
            info.clipId = clip.id;
            info.originalStartTime = clip.startTime;
            clipsInTimeSelection_.push_back(info);
        }
    }
}

void TrackContentPanel::moveClipsWithTimeSelection(double deltaTime) {
    if (clipsInTimeSelection_.empty()) {
        return;
    }

    // Update all clip components visually
    for (const auto& info : clipsInTimeSelection_) {
        double newStartTime = juce::jmax(0.0, info.originalStartTime + deltaTime);

        // Find the clip component and update its position
        for (auto& clipComp : clipComponents_) {
            if (clipComp->getClipId() == info.clipId) {
                const auto* clip = ClipManager::getInstance().getClip(info.clipId);
                if (clip) {
                    int newX = timeToPixel(newStartTime);
                    double clipBts = (clip->autoTempo && clip->lengthBeats > 0.0)
                                         ? clip->lengthBeats
                                         : clip->length * tempoBPM / 60.0;
                    int clipWidth = static_cast<int>(clipBts * currentZoom);
                    clipComp->setBounds(newX, clipComp->getY(), juce::jmax(10, clipWidth),
                                        clipComp->getHeight());
                }
                break;
            }
        }
    }
}

void TrackContentPanel::commitClipsInTimeSelection(double deltaTime) {
    if (clipsInTimeSelection_.empty()) {
        return;
    }

    // Use compound operation to group all moves into single undo step
    if (clipsInTimeSelection_.size() > 1) {
        UndoManager::getInstance().beginCompoundOperation("Move Clips");
    }

    // Commit all clip moves through the undo system
    for (const auto& info : clipsInTimeSelection_) {
        double newStartTime = juce::jmax(0.0, info.originalStartTime + deltaTime);
        auto cmd = std::make_unique<MoveClipCommand>(info.clipId, newStartTime);
        UndoManager::getInstance().executeCommand(std::move(cmd));
    }

    if (clipsInTimeSelection_.size() > 1) {
        UndoManager::getInstance().endCompoundOperation();
    }

    // Clear the captured clips
    clipsInTimeSelection_.clear();

    // Refresh positions from ClipManager
    updateClipComponentPositions();
}

// ============================================================================
// Ghost Clip Rendering (Alt+Drag Duplication Visual Feedback)
// ============================================================================

void TrackContentPanel::setClipGhost(ClipId clipId, const juce::Rectangle<int>& bounds,
                                     const juce::Colour& colour) {
    // Update existing ghost or add new one
    for (auto& ghost : clipGhosts_) {
        if (ghost.clipId == clipId) {
            ghost.bounds = bounds;
            ghost.colour = colour;
            repaint();
            return;
        }
    }

    // Add new ghost
    ClipGhost ghost;
    ghost.clipId = clipId;
    ghost.bounds = bounds;
    ghost.colour = colour;
    clipGhosts_.push_back(ghost);
    repaint();
}

void TrackContentPanel::clearClipGhost(ClipId clipId) {
    auto it = std::remove_if(clipGhosts_.begin(), clipGhosts_.end(),
                             [clipId](const ClipGhost& g) { return g.clipId == clipId; });
    if (it != clipGhosts_.end()) {
        clipGhosts_.erase(it, clipGhosts_.end());
        repaint();
    }
}

void TrackContentPanel::clearAllClipGhosts() {
    if (!clipGhosts_.empty()) {
        clipGhosts_.clear();
        repaint();
    }
}

void TrackContentPanel::paintClipGhosts(juce::Graphics& g) {
    if (clipGhosts_.empty()) {
        return;
    }

    for (const auto& ghost : clipGhosts_) {
        // Draw ghost clip with semi-transparent fill
        g.setColour(ghost.colour.withAlpha(0.3f));
        g.fillRoundedRectangle(ghost.bounds.toFloat(), 4.0f);

        // Draw solid border
        g.setColour(ghost.colour.withAlpha(0.6f));
        g.drawRoundedRectangle(ghost.bounds.toFloat(), 4.0f, 1.5f);

        // Draw inner dotted border to indicate it's a ghost/duplicate
        g.setColour(juce::Colours::white.withAlpha(0.4f));
        auto innerBounds = ghost.bounds.toFloat().reduced(3.0f);

        // Draw simple dotted effect manually
        float dashLength = 4.0f;
        float gapLength = 3.0f;

        // Top edge
        for (float x = innerBounds.getX(); x < innerBounds.getRight();
             x += dashLength + gapLength) {
            float endX = juce::jmin(x + dashLength, innerBounds.getRight());
            g.drawLine(x, innerBounds.getY(), endX, innerBounds.getY(), 1.0f);
        }
        // Bottom edge
        for (float x = innerBounds.getX(); x < innerBounds.getRight();
             x += dashLength + gapLength) {
            float endX = juce::jmin(x + dashLength, innerBounds.getRight());
            g.drawLine(x, innerBounds.getBottom(), endX, innerBounds.getBottom(), 1.0f);
        }
        // Left edge
        for (float y = innerBounds.getY(); y < innerBounds.getBottom();
             y += dashLength + gapLength) {
            float endY = juce::jmin(y + dashLength, innerBounds.getBottom());
            g.drawLine(innerBounds.getX(), y, innerBounds.getX(), endY, 1.0f);
        }
        // Right edge
        for (float y = innerBounds.getY(); y < innerBounds.getBottom();
             y += dashLength + gapLength) {
            float endY = juce::jmin(y + dashLength, innerBounds.getBottom());
            g.drawLine(innerBounds.getRight(), y, innerBounds.getRight(), endY, 1.0f);
        }
    }
}

}  // namespace magda
