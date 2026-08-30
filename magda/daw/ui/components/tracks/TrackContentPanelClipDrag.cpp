// TrackContentPanel — Multi-clip drag, time selection clip handling, and clip ghost methods.
// Split from TrackContentPanel.cpp for file-size compliance.

#include <algorithm>
#include <limits>

#include "../../interaction/ClipDragTargets.hpp"
#include "../../interaction/ClipNudge.hpp"
#include "../clips/ClipComponent.hpp"
#include "TrackContentPanel.hpp"
#include "core/ClipCommands.hpp"
#include "core/ClipPlacementPolicy.hpp"
#include "core/GestureRouter.hpp"
#include "core/SelectionManager.hpp"
#include "core/TempoUtils.hpp"
#include "core/UndoManager.hpp"

namespace magda {

namespace {

// Route through the position-aware tempo facade when wired (message thread);
// fall back to constant-tempo bpm only before injection.
double clipTimelineStart(const ClipInfo& clip, double bpm) {
    if (auto* tc = TimelineController::getCurrent(); tc && tc->tempoMap())
        return clip.getTimelineStart(*tc->tempoMap());
    return clip.getTimelineStart(bpm);
}

double clipTimelineEnd(const ClipInfo& clip, double bpm) {
    if (auto* tc = TimelineController::getCurrent(); tc && tc->tempoMap())
        return clip.getTimelineEnd(*tc->tempoMap());
    return clip.getTimelineEnd(bpm);
}

double clipTimelineLengthBeats(const ClipInfo& clip, double bpm) {
    if (clip.placement.lengthBeats > 0.0)
        return clip.placement.lengthBeats;
    const double resolvedBpm = isValidBpm(bpm) ? bpm : DEFAULT_BPM;
    return clip.getTimelineLength(resolvedBpm) * resolvedBpm / 60.0;
}

// Tracks a move may put this selection on, which is the same question a file
// drop, a keyboard nudge and a clip drag ask, of the same table
// (TrackTypes.hpp).
//
// Asked with the selection in hand rather than of the track alone, because a
// lane can accept one kind and not another: a multi-out lane takes MIDI, whose
// notes play the parent instrument, and has nothing to do with audio. A mixed
// selection therefore needs a track that accepts every kind in it, since a move
// carries the selection as one block and cannot leave half of it behind.
bool canHostClips(const TrackInfo& track, const std::vector<const ClipInfo*>& moving) {
    for (const auto* clip : moving)
        if (clip == nullptr || !trackAcceptsClip(track, *clip))
            return false;
    return !moving.empty();
}

}  // namespace

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
                const double bpm = getTempo();

                // Create MIDI clip by default (tracks are hybrid - can contain both MIDI and audio)
                auto cmd = std::make_unique<CreateClipCommand>(
                    ClipType::MIDI, trackId, BeatPosition{selection.startTime * bpm / 60.0},
                    BeatDuration{length * bpm / 60.0});
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
// Clip Drag Destinations (#2179)
// ============================================================================

void TrackContentPanel::beginClipDragTargets(const std::vector<ClipId>& movingClips,
                                             ClipId anchorClipId) {
    endClipDragTargets();

    auto& clipManager = ClipManager::getInstance();
    auto& trackManager = TrackManager::getInstance();

    // The clips themselves rather than their kinds: a chord lane takes a MIDI
    // clip that is already a progression and not one that merely could have
    // been, and a kind cannot tell those apart.
    std::vector<const ClipInfo*> moving;
    moving.reserve(movingClips.size());
    for (const ClipId clipId : movingClips)
        if (const auto* clip = clipManager.getClip(clipId))
            moving.push_back(clip);
    if (moving.empty())
        return;

    // Frozen tracks are excluded as destinations for the same reason they are
    // refused as sources: dropping a clip into a frozen track's lane would put
    // the model out of step with its rendered audio. Skipped rather than
    // refused, so a frozen lane does not wall off the tracks below it -- which
    // is the whole argument of this feature, applied to the one lane that had
    // it written down first (nudgeSelectedClipsToAdjacentTrack).
    for (int lane = 0; lane < static_cast<int>(visibleTrackIds_.size()); ++lane) {
        const TrackId trackId = visibleTrackIds_[static_cast<size_t>(lane)];
        const auto* track = trackManager.getTrack(trackId);
        if (track == nullptr || track->frozen || !canHostClips(*track, moving))
            continue;
        clipDragHostLanes_.push_back(lane);
        clipDragHostTrackIds_.push_back(trackId);
    }

    // Every clip in the selection needs a slot of its own, because the delta is
    // measured from where each one starts. A selection with a clip on a lane
    // that is not in the set cannot be given one consistently, so the drag
    // keeps its vertical position instead of moving part of the selection.
    int minSlot = std::numeric_limits<int>::max();
    int maxSlot = std::numeric_limits<int>::min();
    for (const auto* clip : moving) {
        const int slot = clipDragSlotOfTrack(clip->trackId);
        if (slot < 0) {
            endClipDragTargets();
            return;
        }
        minSlot = std::min(minSlot, slot);
        maxSlot = std::max(maxSlot, slot);
    }

    clipDragMinSlot_ = minSlot;
    clipDragMaxSlot_ = maxSlot;

    // Measured from the anchor clip's own lane rather than from the pointer's,
    // so a grab a pixel over a lane boundary does not start the gesture with a
    // delta already in it.
    const auto* anchor = clipManager.getClip(anchorClipId);
    clipDragAnchorSlot_ = anchor != nullptr ? clipDragSlotOfTrack(anchor->trackId) : minSlot;
    if (clipDragAnchorSlot_ < 0)
        clipDragAnchorSlot_ = minSlot;
}

void TrackContentPanel::endClipDragTargets() {
    clipDragHostTrackIds_.clear();
    clipDragHostLanes_.clear();
    clipDragAnchorSlot_ = -1;
    clipDragMinSlot_ = 0;
    clipDragMaxSlot_ = 0;
}

int TrackContentPanel::clipDragSlotOfTrack(TrackId trackId) const {
    const auto it = std::find(clipDragHostTrackIds_.begin(), clipDragHostTrackIds_.end(), trackId);
    if (it == clipDragHostTrackIds_.end())
        return -1;
    return static_cast<int>(std::distance(clipDragHostTrackIds_.begin(), it));
}

int TrackContentPanel::clipDragRowAtY(int pointerY) const {
    if (trackLanes.empty())
        return -1;
    if (pointerY < 0)
        return 0;

    // getTrackIndexAtY answers with the track's own lane area and nothing else,
    // so it says -1 both for a pointer below the arrangement and for one inside
    // a track's automation lanes. A drag has to tell those apart: the second is
    // a gap in the middle of the stack, and reading it as "below everything"
    // would send the ghost to the bottom of the arrangement from a position the
    // user is holding halfway up it. An automation lane belongs to the track it
    // was opened under, so the row it falls in is that track's.
    int currentY = 0;
    for (size_t row = 0; row < trackLanes.size(); ++row) {
        const int rowHeight = getTrackTotalHeight(static_cast<int>(row));
        if (pointerY < currentY + rowHeight)
            return static_cast<int>(row);
        currentY += rowHeight;
    }

    // Genuinely past the end of the stack.
    return static_cast<int>(trackLanes.size()) - 1;
}

int TrackContentPanel::clipDragSlotDelta(int pointerY) const {
    if (clipDragAnchorSlot_ < 0)
        return 0;

    // Off either end of the arrangement the pointer counts as being at whichever
    // end it left through, so a drag dragged past the top keeps travelling
    // upwards rather than stalling on the last lane it was over.
    const int lane = clipDragRowAtY(pointerY);
    if (lane < 0)
        return 0;

    const int slot = interaction::nearestHostSlot(clipDragHostLanes_, lane);
    if (slot < 0)
        return 0;

    return interaction::blockSlotDelta(slot - clipDragAnchorSlot_, clipDragMinSlot_,
                                       clipDragMaxSlot_,
                                       static_cast<int>(clipDragHostLanes_.size()));
}

int TrackContentPanel::clipDragTargetLane(TrackId originalTrackId, int slotDelta) const {
    const int slot = clipDragSlotOfTrack(originalTrackId);
    if (slot < 0)
        return -1;

    const int target = slot + slotDelta;
    if (target < 0 || target >= static_cast<int>(clipDragHostLanes_.size()))
        return -1;

    return clipDragHostLanes_[static_cast<size_t>(target)];
}

TrackId TrackContentPanel::clipDragTargetTrackId(TrackId originalTrackId, int slotDelta) const {
    const int slot = clipDragSlotOfTrack(originalTrackId);
    if (slot < 0)
        return originalTrackId;

    const int target = slot + slotDelta;
    if (target < 0 || target >= static_cast<int>(clipDragHostTrackIds_.size()))
        return originalTrackId;

    return clipDragHostTrackIds_[static_cast<size_t>(target)];
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
    // Decide copy-vs-move once, at drag start, from the customisable copy-drag
    // gesture (default Alt) — mirroring the single-clip path so a multi-clip
    // selection copies on drag exactly like a single clip does.
    isMultiClipDuplicating_ = GestureRouter::getInstance().isDuplicateOnDrag(
        GestureContext::Arrangement, juce::ModifierKeys::getCurrentModifiers());
    // Ghost-copy gesture (default Alt+Shift): the copies join their sources'
    // link groups and mirror content.
    isMultiClipGhosting_ = GestureRouter::getInstance().isDuplicateAsGhostOnDrag(
        GestureContext::Arrangement, juce::ModifierKeys::getCurrentModifiers());
    if (isMultiClipGhosting_)
        isMultiClipDuplicating_ = true;
    anchorClipId_ = anchorClipId;
    multiClipDragStartPos_ = startPos;
    multiClipDragDeltaTime_ = 0.0;
    multiClipDragSlotDelta_ = 0;

    // Get the anchor clip's start time and track index
    const auto* anchorClip = ClipManager::getInstance().getClip(anchorClipId);
    if (anchorClip) {
        multiClipDragStartTime_ = clipTimelineStart(*anchorClip, tempoBPM);
    }
    beginClipDragTargets(std::vector<ClipId>(selectedClips.begin(), selectedClips.end()),
                         anchorClipId);

    // Store original positions of all selected clips
    multiClipDragInfos_.clear();
    for (ClipId clipId : selectedClips) {
        const auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip) {
            ClipDragInfo info;
            info.clipId = clipId;
            info.originalStartTime = clipTimelineStart(*clip, tempoBPM);
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

    // Copy-vs-move was latched in startMultiClipDrag from the copy-drag
    // gesture; updateMultiClipDrag only renders/commits that decision.

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
    multiClipDragDeltaTime_ =
        actualDeltaTime;  // Store for finishMultiClipDrag — no pixel round-trip

    // Vertical travel, counted in lanes that can hold the selection rather than
    // in lanes: a lane the drag cannot land on is stepped over, so it is not
    // travel, and the same delta then means the same thing for every clip in
    // the selection however the refusing lanes are distributed between them.
    multiClipDragSlotDelta_ = clipDragSlotDelta(currentPos.y);

    // The ghost is drawn where the commit will put the clip, from the same
    // resolution of the same pointer position, because a ghost that lands where
    // the commit refuses is the defect this replaced. A clip with nowhere to go
    // stays on its own lane.
    const auto ghostLaneFor = [this](TrackId originalTrackId, int originalLane) {
        const int lane = clipDragTargetLane(originalTrackId, multiClipDragSlotDelta_);
        return lane >= 0 ? lane : originalLane;
    };

    if (isMultiClipDuplicating_) {
        // Copy-on-drag: show ghosts at NEW positions, keep originals in place
        for (const auto& dragInfo : multiClipDragInfos_) {
            double newStartTime = juce::jmax(0.0, dragInfo.originalStartTime + actualDeltaTime);
            const int targetTrackIdx =
                ghostLaneFor(dragInfo.originalTrackId, dragInfo.originalTrackIndex);

            const auto* clip = ClipManager::getInstance().getClip(dragInfo.clipId);
            if (clip) {
                int ghostX = beatsToPixel(newStartTime * tempoBPM / 60.0);
                double ghostBeats = clipTimelineLengthBeats(*clip, tempoBPM);
                int ghostWidth = static_cast<int>(std::round(ghostBeats * currentZoom));
                auto trackArea = getTrackLaneArea(targetTrackIdx);
                setClipGhost(dragInfo.clipId,
                             {ghostX, trackArea.getY() + 2, juce::jmax(10, ghostWidth),
                              trackArea.getHeight() - 4},
                             clip->colour);
            }
        }
    } else if (multiClipDragSlotDelta_ != 0) {
        // Cross-track move: show ghosts on target track, keep originals in place
        for (const auto& dragInfo : multiClipDragInfos_) {
            double newStartTime = juce::jmax(0.0, dragInfo.originalStartTime + actualDeltaTime);
            const int targetTrackIdx =
                ghostLaneFor(dragInfo.originalTrackId, dragInfo.originalTrackIndex);
            const auto* clip = ClipManager::getInstance().getClip(dragInfo.clipId);
            if (clip) {
                int ghostX = beatsToPixel(newStartTime * tempoBPM / 60.0);
                double ghostBeats = clipTimelineLengthBeats(*clip, tempoBPM);
                int ghostWidth = static_cast<int>(std::round(ghostBeats * currentZoom));
                auto trackArea = getTrackLaneArea(targetTrackIdx);
                setClipGhost(dragInfo.clipId,
                             {ghostX, trackArea.getY() + 2, juce::jmax(10, ghostWidth),
                              trackArea.getHeight() - 4},
                             clip->colour);
            }
        }
    } else {
        // Same-track move: clear any stale cross-track ghosts
        clearAllClipGhosts();
        // Update clip component X positions directly
        for (const auto& dragInfo : multiClipDragInfos_) {
            double newStartTime = juce::jmax(0.0, dragInfo.originalStartTime + actualDeltaTime);
            for (auto& clipComp : clipComponents_) {
                if (clipComp->getClipId() == dragInfo.clipId) {
                    int newX = beatsToPixel(newStartTime * tempoBPM / 60.0);
                    clipComp->setTopLeftPosition(newX, clipComp->getY());
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

    // Use the deltas stored during updateMultiClipDrag — never re-derive from pixels
    double actualDeltaTime = multiClipDragDeltaTime_;
    int slotDelta = multiClipDragSlotDelta_;

    bool hasTrackChange = slotDelta != 0;
    bool isCompound = multiClipDragInfos_.size() > 1 || hasTrackChange;
    std::unordered_set<ClipId> duplicatedClipIds;

    if (isMultiClipDuplicating_) {
        // Copy-on-drag: create duplicates at final positions through undo system
        if (isCompound) {
            UndoManager::getInstance().beginCompoundOperation(
                isMultiClipGhosting_ ? "Duplicate Clips as Ghosts" : "Duplicate Clips");
        }

        std::vector<std::unique_ptr<DuplicateClipCommand>> commands;
        for (const auto& dragInfo : multiClipDragInfos_) {
            double newStartTime = juce::jmax(0.0, dragInfo.originalStartTime + actualDeltaTime);
            TrackId targetTrackId = clipDragTargetTrackId(dragInfo.originalTrackId, slotDelta);
            const double bpm = getTempo();
            auto cmd = std::make_unique<DuplicateClipCommand>(
                dragInfo.clipId, BeatPosition{newStartTime * bpm / 60.0}, targetTrackId, bpm, -1,
                isMultiClipGhosting_);
            commands.push_back(std::move(cmd));
        }

        for (auto& cmd : commands) {
            DuplicateClipCommand* cmdPtr = cmd.get();
            UndoManager::getInstance().executeCommand(std::move(cmd));
            ClipId dupId = cmdPtr->getDuplicatedClipId();
            if (dupId != INVALID_CLIP_ID) {
                duplicatedClipIds.insert(dupId);
            }
        }

        if (isCompound) {
            UndoManager::getInstance().endCompoundOperation();
        }
    } else {
        // Normal move: apply to original clips through undo system
        if (isCompound) {
            UndoManager::getInstance().beginCompoundOperation("Move Clips");
        }

        for (const auto& dragInfo : multiClipDragInfos_) {
            double newStartTime = juce::jmax(0.0, dragInfo.originalStartTime + actualDeltaTime);
            const double bpm = getTempo();
            auto cmd = std::make_unique<MoveClipCommand>(
                dragInfo.clipId, BeatPosition{newStartTime * bpm / 60.0}, bpm);
            UndoManager::getInstance().executeCommand(std::move(cmd));

            // Move to new track if needed
            TrackId targetTrackId = clipDragTargetTrackId(dragInfo.originalTrackId, slotDelta);
            if (targetTrackId != dragInfo.originalTrackId) {
                // Asked before the command is built, not because the command
                // would let it through -- it refuses the same targets -- but
                // because a refused command is still an undo step that does
                // nothing, and the clip stays where it was either way.
                const auto* target = TrackManager::getInstance().getTrack(targetTrackId);
                const auto* dragged = ClipManager::getInstance().getClip(dragInfo.clipId);
                if (target != nullptr && dragged != nullptr &&
                    trackAcceptsClip(*target, *dragged)) {
                    auto trackCmd =
                        std::make_unique<MoveClipToTrackCommand>(dragInfo.clipId, targetTrackId);
                    UndoManager::getInstance().executeCommand(std::move(trackCmd));
                }
            }
        }

        if (isCompound) {
            UndoManager::getInstance().endCompoundOperation();
        }
    }

    // Clean up
    isMovingMultipleClips_ = false;
    isMultiClipDuplicating_ = false;
    isMultiClipGhosting_ = false;
    anchorClipId_ = INVALID_CLIP_ID;
    multiClipDragInfos_.clear();
    multiClipDuplicateIds_.clear();
    multiClipDragSlotDelta_ = 0;
    endClipDragTargets();

    // Refresh positions from ClipManager
    updateClipComponentPositions();

    // Select duplicated clips instead of originals.
    // Must clear isDragging_ on source clips first — clipSelectionChanged
    // ignores updates while isDragging_ is true, so sources would stay
    // visually selected.
    if (!duplicatedClipIds.empty()) {
        for (auto& clipComp : clipComponents_) {
            clipComp->clearDragging();
        }
        SelectionManager::getInstance().selectClips(duplicatedClipIds);
    }
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
    isMultiClipGhosting_ = false;
    anchorClipId_ = INVALID_CLIP_ID;
    multiClipDragInfos_.clear();
    multiClipDuplicateIds_.clear();
    multiClipDragSlotDelta_ = 0;
    endClipDragTargets();
}

// ============================================================================
// Keyboard Clip Nudging (#1957)
// ============================================================================

namespace {

// The clips a nudge acts on: the arrangement half of the selection. Session
// clips live in a scene grid, not on the timeline, and are left alone even
// when a session view put them in the same selection.
std::vector<ClipId> selectedArrangementClips() {
    std::vector<ClipId> clips;
    auto& clipManager = ClipManager::getInstance();
    for (ClipId clipId : SelectionManager::getInstance().getSelectedClips()) {
        const auto* clip = clipManager.getClip(clipId);
        if (clip != nullptr && clip->view == ClipView::Arrangement)
            clips.push_back(clipId);
    }
    return clips;
}

// Frozen tracks are rendered to audio, so their clips must not move out from
// under the render. Clips there stay selectable, and both the mouse drag
// (ClipComponent::mouseDrag) and the destructive context actions already
// refuse them; the keyboard has to refuse them too or the shortcut becomes a
// hole in the freeze invariant.
bool isOnFrozenTrack(ClipId clipId) {
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (clip == nullptr)
        return false;
    const auto* track = TrackManager::getInstance().getTrack(clip->trackId);
    return track != nullptr && track->frozen;
}

bool anyClipOnFrozenTrack(const std::vector<ClipId>& clips) {
    return std::any_of(clips.begin(), clips.end(), isOnFrozenTrack);
}

}  // namespace

bool TrackContentPanel::nudgeSelectedClipsHorizontally(int direction) {
    if (direction == 0 || timelineController == nullptr)
        return false;

    const auto clips = selectedArrangementClips();
    if (clips.empty() || anyClipOnFrozenTrack(clips))
        return false;

    auto& clipManager = ClipManager::getInstance();
    double anchorStartBeat = std::numeric_limits<double>::max();
    for (ClipId clipId : clips) {
        if (const auto* clip = clipManager.getClip(clipId))
            anchorStartBeat = std::min(anchorStartBeat, clip->placement.startBeat);
    }

    // The earliest clip anchors the move so the selection keeps its internal
    // spacing, the same contract a multi-clip drag has.
    //
    // The step comes from whichever grid is actually drawn. In Seconds mode
    // that is a 1/2/5-based interval in seconds, unrelated to the beat
    // subdivision getSnapBeatFraction() returns — at 90 BPM and 40 px/beat the
    // ruler shows 1 s lines while the beat fraction is 2 beats (1.333 s), so
    // taking beats there would move the clip off every visible line. Resolve
    // in the displayed domain, then convert the target back to beats.
    const auto& state = timelineController->getState();
    interaction::HorizontalNudge nudge;
    nudge.snapToGrid = state.display.snapEnabled;
    nudge.direction = direction;

    double deltaBeats = 0.0;
    if (state.display.timeDisplayMode == TimeDisplayMode::Seconds) {
        const double anchorSeconds = beatsToSeconds(anchorStartBeat);
        nudge.anchorPosition = anchorSeconds;
        nudge.gridStep = state.getSnapInterval();

        const double deltaSeconds = interaction::horizontalDelta(nudge);
        if (deltaSeconds == 0.0)
            return false;
        deltaBeats = secondsToBeats(anchorSeconds + deltaSeconds) - anchorStartBeat;
    } else {
        nudge.anchorPosition = anchorStartBeat;
        nudge.gridStep = state.getSnapBeatFraction();
        deltaBeats = interaction::horizontalDelta(nudge);
    }

    if (deltaBeats == 0.0)
        return false;

    // Apply back-to-front relative to travel. Each MoveClipCommand executes
    // immediately and ClipManager::moveClipBeats resolves overlaps against
    // every other clip on the track — including selected ones that have not
    // moved yet. Nudging two adjacent selected clips rightwards would
    // otherwise let the leader land on top of its neighbour and trim or delete
    // it, with the surviving clip decided by unordered_set iteration order.
    // Moving the far clip first keeps the path clear.
    std::vector<ClipId> ordered = clips;
    std::sort(ordered.begin(), ordered.end(), [&](ClipId lhs, ClipId rhs) {
        const auto* a = clipManager.getClip(lhs);
        const auto* b = clipManager.getClip(rhs);
        if (a == nullptr || b == nullptr)
            return false;
        return direction > 0 ? a->placement.startBeat > b->placement.startBeat
                             : a->placement.startBeat < b->placement.startBeat;
    });

    // Always compound, even for one clip: consecutive MoveClipCommands on the
    // same clip merge into a single history entry, which would fold a run of
    // presses into one undo. A press is an action and gets its own step.
    const double bpm = getTempo();
    CompoundOperationScope undoScope("Nudge Clips");
    for (ClipId clipId : ordered) {
        const auto* clip = clipManager.getClip(clipId);
        if (clip == nullptr)
            continue;
        UndoManager::getInstance().executeCommand(std::make_unique<MoveClipCommand>(
            clipId, BeatPosition{clip->placement.startBeat + deltaBeats}, bpm));
    }

    return true;
}

bool TrackContentPanel::nudgeSelectedClipsToAdjacentTrack(int direction) {
    if (direction == 0)
        return false;

    const auto clips = selectedArrangementClips();
    if (clips.empty() || anyClipOnFrozenTrack(clips))
        return false;

    // Frozen tracks are excluded as destinations for the same reason they are
    // refused as sources: dropping a clip into a frozen track's lane would put
    // the model out of step with its rendered audio. Skipping them (rather
    // than refusing) keeps a frozen lane from walling off the tracks below it.
    auto& trackManager = TrackManager::getInstance();
    auto& clipManager = ClipManager::getInstance();

    // What is moving, so a destination can be judged against it. The clips
    // themselves rather than their kinds: a chord lane takes a MIDI clip that
    // is already a progression and not one that merely could have been, and a
    // kind cannot tell those apart.
    std::vector<const ClipInfo*> moving;
    moving.reserve(clips.size());
    for (const ClipId clipId : clips)
        if (const auto* clip = clipManager.getClip(clipId))
            moving.push_back(clip);

    std::vector<TrackId> hostTrackIds;
    for (TrackId trackId : visibleTrackIds_) {
        const auto* track = trackManager.getTrack(trackId);
        if (track != nullptr && canHostClips(*track, moving) && !track->frozen)
            hostTrackIds.push_back(trackId);
    }
    if (hostTrackIds.size() < 2)
        return false;

    struct ClipTrackIndex {
        ClipId clipId;
        int trackIndex;
    };
    std::vector<ClipTrackIndex> moves;
    moves.reserve(clips.size());

    int minTrackIndex = std::numeric_limits<int>::max();
    int maxTrackIndex = std::numeric_limits<int>::min();
    for (ClipId clipId : clips) {
        const auto* clip = clipManager.getClip(clipId);
        if (clip == nullptr)
            continue;
        auto it = std::find(hostTrackIds.begin(), hostTrackIds.end(), clip->trackId);
        if (it == hostTrackIds.end())
            return false;  // e.g. a chord clip: the selection moves whole or not at all

        const int trackIndex = static_cast<int>(std::distance(hostTrackIds.begin(), it));
        moves.push_back({clipId, trackIndex});
        minTrackIndex = std::min(minTrackIndex, trackIndex);
        maxTrackIndex = std::max(maxTrackIndex, trackIndex);
    }
    if (moves.empty())
        return false;

    interaction::VerticalNudge nudge;
    nudge.minTrackIndex = minTrackIndex;
    nudge.maxTrackIndex = maxTrackIndex;
    nudge.trackCount = static_cast<int>(hostTrackIds.size());
    nudge.direction = direction;

    const int trackDelta = interaction::verticalTrackDelta(nudge);
    if (trackDelta == 0)
        return false;

    // Same back-to-front rule as the horizontal path: landing on a track a
    // still-unmoved selected clip occupies would let overlap resolution trim
    // or delete that clip. Vacate the far track first.
    std::sort(moves.begin(), moves.end(), [direction](const auto& lhs, const auto& rhs) {
        return direction > 0 ? lhs.trackIndex > rhs.trackIndex : lhs.trackIndex < rhs.trackIndex;
    });

    CompoundOperationScope undoScope("Move Clips to Track");
    for (const auto& move : moves) {
        const auto targetIndex = static_cast<size_t>(move.trackIndex + trackDelta);
        UndoManager::getInstance().executeCommand(
            std::make_unique<MoveClipToTrackCommand>(move.clipId, hostTrackIds[targetIndex]));
    }

    return true;
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

        double clipStart = clipTimelineStart(clip, getTempo());
        double clipEnd = clipTimelineEnd(clip, getTempo());
        bool needsLeft = (clipStart < start && clipEnd > start);
        bool needsRight = (clipStart < end && clipEnd > end);

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
            const double bpm = getTempo();
            auto cmd = std::make_unique<SplitClipCommand>(info.clipId,
                                                          BeatPosition{start * bpm / 60.0}, bpm);
            auto* cmdPtr = cmd.get();
            UndoManager::getInstance().executeCommand(std::move(cmd));
            // The right piece (from start onward) is the one that may need a right split
            rightSideId = cmdPtr->getRightClipId();
        }

        // Split at right boundary — use the right piece from the left split if applicable
        if (info.needsRightSplit) {
            const auto* clip = ClipManager::getInstance().getClip(rightSideId);
            if (clip && end > clipTimelineStart(*clip, getTempo()) &&
                end < clipTimelineEnd(*clip, getTempo())) {
                const double bpm = getTempo();
                auto cmd = std::make_unique<SplitClipCommand>(rightSideId,
                                                              BeatPosition{end * bpm / 60.0}, bpm);
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
        double clipStart = clipTimelineStart(clip, getTempo());
        double clipEnd = clipTimelineEnd(clip, getTempo());
        if (clipStart < selection.endTime && clipEnd > selection.startTime) {
            // Clip overlaps with selection - capture it
            TimeSelectionClipInfo info;
            info.clipId = clip.id;
            info.originalStartTime = clipStart;
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
                    int newX = beatsToPixel(newStartTime * tempoBPM / 60.0);
                    double clipBts = clipTimelineLengthBeats(*clip, tempoBPM);
                    int clipWidth = static_cast<int>(std::round(clipBts * currentZoom));
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
        const double bpm = getTempo();
        auto cmd = std::make_unique<MoveClipCommand>(info.clipId,
                                                     BeatPosition{newStartTime * bpm / 60.0}, bpm);
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
            repaintVisible();
            return;
        }
    }

    // Add new ghost
    ClipGhost ghost;
    ghost.clipId = clipId;
    ghost.bounds = bounds;
    ghost.colour = colour;
    clipGhosts_.push_back(ghost);
    repaintVisible();
}

void TrackContentPanel::clearClipGhost(ClipId clipId) {
    auto it = std::remove_if(clipGhosts_.begin(), clipGhosts_.end(),
                             [clipId](const ClipGhost& g) { return g.clipId == clipId; });
    if (it != clipGhosts_.end()) {
        clipGhosts_.erase(it, clipGhosts_.end());
        repaintVisible();
    }
}

void TrackContentPanel::clearAllClipGhosts() {
    if (!clipGhosts_.empty()) {
        clipGhosts_.clear();
        repaintVisible();
    }
}

juce::Rectangle<int> TrackContentPanel::getClipGhostBounds(ClipId clipId) const {
    for (const auto& ghost : clipGhosts_)
        if (ghost.clipId == clipId)
            return ghost.bounds;
    return {};
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
