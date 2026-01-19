#include "TrackContentPanel.hpp"

#include <functional>
#include <iostream>

#include "../../layout/LayoutConfig.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../../utils/TimelineUtils.hpp"
#include "../clips/ClipComponent.hpp"
#include "Config.hpp"
#include "core/SelectionManager.hpp"

namespace magica {

TrackContentPanel::TrackContentPanel() {
    // Load configuration values
    auto& config = magica::Config::getInstance();
    timelineLength = config.getDefaultTimelineLength();

    // Set up the component
    setSize(1000, 200);
    setOpaque(true);

    // Register as TrackManager listener
    TrackManager::getInstance().addListener(this);

    // Register as ClipManager listener
    ClipManager::getInstance().addListener(this);

    // Register as ViewModeController listener
    ViewModeController::getInstance().addListener(this);
    currentViewMode_ = ViewModeController::getInstance().getViewMode();

    // Build tracks from TrackManager
    tracksChanged();

    // Build clips from ClipManager
    rebuildClipComponents();
}

TrackContentPanel::~TrackContentPanel() {
    // Unregister from TrackManager
    TrackManager::getInstance().removeListener(this);

    // Unregister from ClipManager
    ClipManager::getInstance().removeListener(this);

    // Unregister from ViewModeController
    ViewModeController::getInstance().removeListener(this);

    // Unregister from controller if we have one
    if (timelineController) {
        timelineController->removeListener(this);
    }
}

void TrackContentPanel::viewModeChanged(ViewMode mode, const AudioEngineProfile& /*profile*/) {
    currentViewMode_ = mode;
    tracksChanged();  // Rebuild with new visibility settings
}

void TrackContentPanel::tracksChanged() {
    // Rebuild track lanes from TrackManager
    trackLanes.clear();
    visibleTrackIds_.clear();
    selectedTrackIndex = -1;

    // Build visible tracks list (respecting hierarchy)
    auto& trackManager = TrackManager::getInstance();
    auto topLevelTracks = trackManager.getVisibleTopLevelTracks(currentViewMode_);

    // Helper lambda to add track and its visible children recursively
    std::function<void(TrackId, int)> addTrackRecursive = [&](TrackId trackId, int depth) {
        const auto* track = trackManager.getTrack(trackId);
        if (!track || !track->isVisibleIn(currentViewMode_))
            return;

        visibleTrackIds_.push_back(trackId);

        auto lane = std::make_unique<TrackLane>();
        // Use height from view settings
        lane->height = track->viewSettings.getHeight(currentViewMode_);
        trackLanes.push_back(std::move(lane));

        // Add children if group is not collapsed
        if (track->isGroup() && !track->isCollapsedIn(currentViewMode_)) {
            for (auto childId : track->childIds) {
                addTrackRecursive(childId, depth + 1);
            }
        }
    };

    // Add all visible top-level tracks (and their children)
    for (auto trackId : topLevelTracks) {
        addTrackRecursive(trackId, 0);
    }

    resized();
    repaint();
}

void TrackContentPanel::setController(TimelineController* controller) {
    // Unregister from old controller
    if (timelineController) {
        timelineController->removeListener(this);
    }

    timelineController = controller;

    // Register with new controller
    if (timelineController) {
        timelineController->addListener(this);

        // Sync initial state
        const auto& state = timelineController->getState();
        timelineLength = state.timelineLength;
        currentZoom = state.zoom.horizontalZoom;
        displayMode = state.display.timeDisplayMode;
        tempoBPM = state.tempo.bpm;
        timeSignatureNumerator = state.tempo.timeSignatureNumerator;
        timeSignatureDenominator = state.tempo.timeSignatureDenominator;

        repaint();
    }
}

// ===== TimelineStateListener Implementation =====

void TrackContentPanel::timelineStateChanged(const TimelineState& state) {
    // General state change - sync cached values
    timelineLength = state.timelineLength;
    displayMode = state.display.timeDisplayMode;
    tempoBPM = state.tempo.bpm;
    timeSignatureNumerator = state.tempo.timeSignatureNumerator;
    timeSignatureDenominator = state.tempo.timeSignatureDenominator;
    repaint();
}

void TrackContentPanel::zoomStateChanged(const TimelineState& state) {
    currentZoom = state.zoom.horizontalZoom;
    resized();
    repaint();
}

void TrackContentPanel::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::TRACK_BACKGROUND));

    // Grid is now drawn by GridOverlayComponent in MainView
    // This component only draws track lanes with horizontal separators
    for (size_t i = 0; i < trackLanes.size(); ++i) {
        auto laneArea = getTrackLaneArea(static_cast<int>(i));
        if (laneArea.intersects(getLocalBounds())) {
            paintTrackLane(g, *trackLanes[i], laneArea, static_cast<int>(i) == selectedTrackIndex,
                           static_cast<int>(i));
        }
    }

    // Draw marquee selection rectangle
    paintMarqueeRect(g);
}

void TrackContentPanel::resized() {
    // Update size based on zoom and timeline length
    int contentWidth = static_cast<int>(timelineLength * currentZoom);
    int contentHeight = getTotalTracksHeight();

    setSize(juce::jmax(contentWidth, getWidth()), juce::jmax(contentHeight, getHeight()));
}

void TrackContentPanel::addTrack() {
    auto lane = std::make_unique<TrackLane>();
    trackLanes.push_back(std::move(lane));

    resized();
    repaint();
}

void TrackContentPanel::removeTrack(int index) {
    if (index >= 0 && index < trackLanes.size()) {
        trackLanes.erase(trackLanes.begin() + index);

        if (selectedTrackIndex == index) {
            selectedTrackIndex = -1;
        } else if (selectedTrackIndex > index) {
            selectedTrackIndex--;
        }

        resized();
        repaint();
    }
}

void TrackContentPanel::selectTrack(int index) {
    if (index >= 0 && index < trackLanes.size()) {
        selectedTrackIndex = index;

        if (onTrackSelected) {
            onTrackSelected(index);
        }

        repaint();
    }
}

int TrackContentPanel::getNumTracks() const {
    return static_cast<int>(trackLanes.size());
}

void TrackContentPanel::setTrackHeight(int trackIndex, int height) {
    if (trackIndex >= 0 && trackIndex < trackLanes.size()) {
        height = juce::jlimit(MIN_TRACK_HEIGHT, MAX_TRACK_HEIGHT, height);
        trackLanes[trackIndex]->height = height;

        resized();
        repaint();

        if (onTrackHeightChanged) {
            onTrackHeightChanged(trackIndex, height);
        }
    }
}

int TrackContentPanel::getTrackHeight(int trackIndex) const {
    if (trackIndex >= 0 && trackIndex < trackLanes.size()) {
        return trackLanes[trackIndex]->height;
    }
    return DEFAULT_TRACK_HEIGHT;
}

void TrackContentPanel::setZoom(double zoom) {
    currentZoom = juce::jmax(0.1, zoom);
    updateClipComponentPositions();
    resized();
    repaint();
}

void TrackContentPanel::setVerticalZoom(double zoom) {
    verticalZoom = juce::jlimit(0.5, 3.0, zoom);
    updateClipComponentPositions();
    resized();
    repaint();
}

void TrackContentPanel::setTimelineLength(double lengthInSeconds) {
    timelineLength = lengthInSeconds;
    resized();
    repaint();
}

void TrackContentPanel::setTimeDisplayMode(TimeDisplayMode mode) {
    displayMode = mode;
    repaint();
}

void TrackContentPanel::setTempo(double bpm) {
    tempoBPM = juce::jlimit(20.0, 999.0, bpm);
    repaint();
}

void TrackContentPanel::setTimeSignature(int numerator, int denominator) {
    timeSignatureNumerator = juce::jlimit(1, 16, numerator);
    timeSignatureDenominator = juce::jlimit(1, 16, denominator);
    repaint();
}

int TrackContentPanel::getTotalTracksHeight() const {
    int totalHeight = 0;
    for (const auto& lane : trackLanes) {
        totalHeight += static_cast<int>(lane->height * verticalZoom);
    }
    return totalHeight;
}

int TrackContentPanel::getTrackYPosition(int trackIndex) const {
    int yPosition = 0;
    for (int i = 0; i < trackIndex && i < trackLanes.size(); ++i) {
        yPosition += static_cast<int>(trackLanes[i]->height * verticalZoom);
    }
    return yPosition;
}

void TrackContentPanel::paintTrackLane(juce::Graphics& g, const TrackLane& lane,
                                       juce::Rectangle<int> area, bool isSelected, int trackIndex) {
    // Background (semi-transparent to let grid show through)
    auto bgColour = isSelected ? DarkTheme::getColour(DarkTheme::TRACK_SELECTED)
                               : DarkTheme::getColour(DarkTheme::TRACK_BACKGROUND);
    g.setColour(bgColour.withAlpha(0.7f));
    g.fillRect(area);

    // Border (horizontal separators between tracks)
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(area, 1);
}

juce::Rectangle<int> TrackContentPanel::getTrackLaneArea(int trackIndex) const {
    if (trackIndex < 0 || trackIndex >= trackLanes.size()) {
        return {};
    }

    int yPosition = getTrackYPosition(trackIndex);
    int height = static_cast<int>(trackLanes[trackIndex]->height * verticalZoom);

    return juce::Rectangle<int>(0, yPosition, getWidth(), height);
}

bool TrackContentPanel::isInSelectableArea(int x, int y) const {
    // Check if we're in an empty track area (not on a clip)
    // For now, entire track area is selectable since we don't have clips yet
    // In the future, check if clicking on upper half of clips
    for (size_t i = 0; i < trackLanes.size(); ++i) {
        if (getTrackLaneArea(static_cast<int>(i)).contains(x, y)) {
            return true;
        }
    }
    return false;
}

double TrackContentPanel::pixelToTime(int pixel) const {
    return TimelineUtils::pixelToTime(pixel, currentZoom, LEFT_PADDING);
}

int TrackContentPanel::timeToPixel(double time) const {
    return TimelineUtils::timeToPixel(time, currentZoom, LEFT_PADDING);
}

int TrackContentPanel::getTrackIndexAtY(int y) const {
    int currentY = 0;
    for (size_t i = 0; i < trackLanes.size(); ++i) {
        int trackHeight = static_cast<int>(trackLanes[i]->height * verticalZoom);
        if (y >= currentY && y < currentY + trackHeight) {
            return static_cast<int>(i);
        }
        currentY += trackHeight;
    }
    return -1;  // Not in any track
}

bool TrackContentPanel::isOnExistingSelection(int x, int y) const {
    // Check if there's an active selection in the controller
    if (!timelineController) {
        return false;
    }

    const auto& selection = timelineController->getState().selection;
    if (!selection.isActive()) {
        return false;
    }

    // Check horizontal bounds (time-based)
    double clickTime = pixelToTime(x);
    if (clickTime < selection.startTime || clickTime > selection.endTime) {
        return false;
    }

    // Check vertical bounds (track-based)
    int trackIndex = getTrackIndexAtY(y);
    if (trackIndex < 0) {
        return false;
    }

    // Check if this track is part of the selection
    return selection.includesTrack(trackIndex);
}

void TrackContentPanel::mouseDown(const juce::MouseEvent& event) {
    // Store initial mouse position for click vs drag detection
    mouseDownX = event.x;
    mouseDownY = event.y;

    // Capture Shift state and starting track index for per-track selection
    isShiftHeld = event.mods.isShiftDown();
    selectionStartTrackIndex = getTrackIndexAtY(event.y);

    // Reset drag type
    currentDragType_ = DragType::None;

    // Select track based on click position
    for (size_t i = 0; i < trackLanes.size(); ++i) {
        if (getTrackLaneArea(static_cast<int>(i)).contains(event.getPosition())) {
            selectTrack(static_cast<int>(i));
            break;
        }
    }

    // Zone-based behavior:
    // Upper half of track = clip operations
    // Lower half of track = time selection operations
    bool inUpperZone = isInUpperTrackZone(event.y);
    bool onClip = getClipComponentAt(event.x, event.y) != nullptr;

    if (inUpperZone) {
        // UPPER ZONE: Clip operations
        if (!onClip) {
            // Clicked empty space in upper zone - deselect clips (unless Cmd held)
            if (!event.mods.isCommandDown()) {
                SelectionManager::getInstance().clearSelection();
            }
        }
        // If on clip, the ClipComponent handles mouse events
        // Prepare for potential marquee if they drag (but not playhead move)
        // Upper zone clicks don't move playhead - only lower zone does
        if (!onClip && isInSelectableArea(event.x, event.y)) {
            isCreatingSelection = true;
            isMovingSelection = false;
            currentDragType_ = DragType::Marquee;  // Mark as marquee so click doesn't move playhead
        }
    } else {
        // LOWER ZONE: Time selection operations
        if (isOnExistingSelection(event.x, event.y)) {
            // Clicked inside existing time selection - prepare to move it
            const auto& selection = timelineController->getState().selection;
            isMovingSelection = true;
            isCreatingSelection = false;
            currentDragType_ = DragType::MoveSelection;
            moveDragStartTime = pixelToTime(event.x);
            moveSelectionOriginalStart = selection.startTime;
            moveSelectionOriginalEnd = selection.endTime;
            moveSelectionOriginalTracks = selection.trackIndices;

            // Capture all clips within the time selection
            captureClipsInTimeSelection();
            return;
        } else {
            // Clicked outside time selection in lower zone - clear it and start new one
            if (timelineController && timelineController->getState().selection.isActive()) {
                // Clear existing time selection
                if (onTimeSelectionChanged) {
                    onTimeSelectionChanged(-1.0, -1.0, {});
                }
            }
            // Prepare for new time selection
            if (isInSelectableArea(event.x, event.y)) {
                isCreatingSelection = true;
                isMovingSelection = false;
                selectionStartTime = juce::jmax(0.0, pixelToTime(event.x));

                // Apply snap to grid if callback is set
                if (snapTimeToGrid) {
                    selectionStartTime = snapTimeToGrid(selectionStartTime);
                }

                selectionEndTime = selectionStartTime;
            }
        }
    }
}

void TrackContentPanel::mouseDrag(const juce::MouseEvent& event) {
    if (isMovingSelection) {
        // Calculate time delta from drag start
        double currentTime = pixelToTime(event.x);
        double deltaTime = currentTime - moveDragStartTime;

        // Calculate new selection bounds
        double newStart = moveSelectionOriginalStart + deltaTime;
        double newEnd = moveSelectionOriginalEnd + deltaTime;

        // Apply snap to grid if callback is set
        if (snapTimeToGrid) {
            double snappedStart = snapTimeToGrid(newStart);
            double snapDelta = snappedStart - newStart;
            newStart = snappedStart;
            newEnd += snapDelta;
            deltaTime += snapDelta;  // Adjust delta for clip movement
        }

        // Clamp to timeline bounds
        double duration = moveSelectionOriginalEnd - moveSelectionOriginalStart;
        if (newStart < 0) {
            newStart = 0;
            newEnd = duration;
            deltaTime = -moveSelectionOriginalStart;
        }
        if (newEnd > timelineLength) {
            newEnd = timelineLength;
            newStart = timelineLength - duration;
            deltaTime = newStart - moveSelectionOriginalStart;
        }

        // Move clips visually along with the time selection
        moveClipsWithTimeSelection(deltaTime);

        // Notify about selection change (preserve original track indices)
        if (onTimeSelectionChanged) {
            onTimeSelectionChanged(newStart, newEnd, moveSelectionOriginalTracks);
        }
    } else if (isMarqueeActive_) {
        // Already in marquee mode - continue updating
        updateMarqueeSelection(event.getPosition());
    } else if (isCreatingSelection) {
        int deltaX = std::abs(event.x - mouseDownX);
        int deltaY = std::abs(event.y - mouseDownY);
        int dragDistance = juce::jmax(deltaX, deltaY);

        // Determine mode based on where the drag STARTED (upper vs lower track zone)
        if (currentDragType_ == DragType::None && dragDistance > DRAG_START_THRESHOLD) {
            // Upper half of track = marquee selection, lower half = time selection
            if (isInUpperTrackZone(mouseDownY)) {
                // Start marquee selection
                isCreatingSelection = false;
                startMarqueeSelection(juce::Point<int>(mouseDownX, mouseDownY));
                updateMarqueeSelection(event.getPosition());
                return;
            } else {
                // Start time selection
                currentDragType_ = DragType::TimeSelection;
            }
        }

        // Update time selection end time
        selectionEndTime = juce::jmax(0.0, juce::jmin(timelineLength, pixelToTime(event.x)));

        // Apply snap to grid if callback is set
        if (snapTimeToGrid) {
            selectionEndTime = snapTimeToGrid(selectionEndTime);
        }

        // Track the current track under the mouse for multi-track selection
        selectionEndTrackIndex = getTrackIndexAtY(event.y);

        // Clamp to valid track range (handle dragging above/below track area)
        if (selectionEndTrackIndex < 0) {
            // If above first track, select first track; if below last, select last
            if (event.y < 0) {
                selectionEndTrackIndex = 0;
            } else {
                selectionEndTrackIndex = static_cast<int>(trackLanes.size()) - 1;
            }
        }

        // Build track indices set: include all tracks between start and end
        std::set<int> trackIndices;
        if (isShiftHeld) {
            // Shift held = all tracks (empty set)
        } else if (selectionStartTrackIndex >= 0 && selectionEndTrackIndex >= 0) {
            // Include all tracks from start to end (inclusive)
            int minTrack = juce::jmin(selectionStartTrackIndex, selectionEndTrackIndex);
            int maxTrack = juce::jmax(selectionStartTrackIndex, selectionEndTrackIndex);
            for (int i = minTrack; i <= maxTrack; ++i) {
                trackIndices.insert(i);
            }
        }

        // Notify about selection change
        if (onTimeSelectionChanged) {
            double start = juce::jmin(selectionStartTime, selectionEndTime);
            double end = juce::jmax(selectionStartTime, selectionEndTime);
            onTimeSelectionChanged(start, end, trackIndices);
        }
    }
}

void TrackContentPanel::mouseUp(const juce::MouseEvent& event) {
    if (isMovingSelection) {
        // Calculate final delta time to commit clips
        double currentTime = pixelToTime(event.x);
        double deltaTime = currentTime - moveDragStartTime;

        // Apply same snap logic as in mouseDrag
        double newStart = moveSelectionOriginalStart + deltaTime;
        if (snapTimeToGrid) {
            double snappedStart = snapTimeToGrid(newStart);
            double snapDelta = snappedStart - newStart;
            deltaTime += snapDelta;
        }

        // Clamp to timeline bounds
        double duration = moveSelectionOriginalEnd - moveSelectionOriginalStart;
        newStart = moveSelectionOriginalStart + deltaTime;
        double newEnd = moveSelectionOriginalEnd + deltaTime;
        if (newStart < 0) {
            deltaTime = -moveSelectionOriginalStart;
        }
        if (newEnd > timelineLength) {
            deltaTime = (timelineLength - duration) - moveSelectionOriginalStart;
        }

        // Commit clip positions
        commitClipsInTimeSelection(deltaTime);

        // Finalize move - the selection has already been updated via mouseDrag
        isMovingSelection = false;
        moveDragStartTime = -1.0;
        moveSelectionOriginalStart = -1.0;
        moveSelectionOriginalEnd = -1.0;
        moveSelectionOriginalTracks.clear();
        currentDragType_ = DragType::None;
        return;
    }

    // Handle marquee selection completion
    if (isMarqueeActive_) {
        finishMarqueeSelection(event.mods.isShiftDown());
        currentDragType_ = DragType::None;
        return;
    }

    if (isCreatingSelection) {
        isCreatingSelection = false;

        // Check if this was a click or a drag using pixel-based threshold
        int deltaX = std::abs(event.x - mouseDownX);
        int deltaY = std::abs(event.y - mouseDownY);

        if (deltaX <= DRAG_THRESHOLD && deltaY <= DRAG_THRESHOLD) {
            // It was a click
            // Only move playhead for lower zone (time selection) clicks, not upper zone (clip)
            // clicks
            if (currentDragType_ != DragType::Marquee) {
                double clickTime = pixelToTime(mouseDownX);
                clickTime = juce::jlimit(0.0, timelineLength, clickTime);

                // Apply snap to grid if callback is set
                if (snapTimeToGrid) {
                    clickTime = snapTimeToGrid(clickTime);
                }

                // Schedule playhead change (will be cancelled if double-click detected)
                pendingPlayheadTime = clickTime;
                startTimer(DOUBLE_CLICK_DELAY_MS);
            }
        } else {
            // It was a drag - finalize time selection
            selectionEndTime = juce::jmax(0.0, juce::jmin(timelineLength, pixelToTime(event.x)));

            // Apply snap to grid if callback is set
            if (snapTimeToGrid) {
                selectionEndTime = snapTimeToGrid(selectionEndTime);
            }

            // Get final track index from mouse position
            selectionEndTrackIndex = getTrackIndexAtY(event.y);
            if (selectionEndTrackIndex < 0) {
                if (event.y < 0) {
                    selectionEndTrackIndex = 0;
                } else {
                    selectionEndTrackIndex = static_cast<int>(trackLanes.size()) - 1;
                }
            }

            // Normalize so start < end
            double start = juce::jmin(selectionStartTime, selectionEndTime);
            double end = juce::jmax(selectionStartTime, selectionEndTime);

            // Only create selection if it has meaningful duration
            if (end - start > 0.01) {  // At least 10ms selection
                // Build track indices set: include all tracks between start and end
                std::set<int> trackIndices;
                if (isShiftHeld) {
                    // Shift held = all tracks (empty set)
                } else if (selectionStartTrackIndex >= 0 && selectionEndTrackIndex >= 0) {
                    // Include all tracks from start to end (inclusive)
                    int minTrack = juce::jmin(selectionStartTrackIndex, selectionEndTrackIndex);
                    int maxTrack = juce::jmax(selectionStartTrackIndex, selectionEndTrackIndex);
                    for (int i = minTrack; i <= maxTrack; ++i) {
                        trackIndices.insert(i);
                    }
                }

                if (onTimeSelectionChanged) {
                    onTimeSelectionChanged(start, end, trackIndices);
                }
            }
        }

        selectionStartTime = -1.0;
        selectionEndTime = -1.0;
        selectionStartTrackIndex = -1;
        selectionEndTrackIndex = -1;
        isShiftHeld = false;
        currentDragType_ = DragType::None;
    }
}

void TrackContentPanel::mouseDoubleClick(const juce::MouseEvent& event) {
    // Cancel any pending playhead move (double-click should not move playhead)
    stopTimer();
    pendingPlayheadTime = -1.0;

    // Check if double-clicking on an existing selection -> create clip
    if (isOnExistingSelection(event.x, event.y)) {
        createClipFromTimeSelection();
        // Clear selection after creating clip
        if (onTimeSelectionChanged) {
            onTimeSelectionChanged(-1.0, -1.0, {});
        }
    } else {
        // Double-click on empty area clears selection
        if (onTimeSelectionChanged) {
            onTimeSelectionChanged(-1.0, -1.0, {});
        }
    }
}

void TrackContentPanel::timerCallback() {
    stopTimer();

    // Execute the pending playhead move
    if (pendingPlayheadTime >= 0 && onPlayheadPositionChanged) {
        onPlayheadPositionChanged(pendingPlayheadTime);
    }
    pendingPlayheadTime = -1.0;
}

void TrackContentPanel::mouseMove(const juce::MouseEvent& event) {
    updateCursorForPosition(event.x, event.y);
}

bool TrackContentPanel::isInUpperTrackZone(int y) const {
    int trackIndex = getTrackIndexAtY(y);
    if (trackIndex < 0) {
        return false;
    }

    auto trackArea = getTrackLaneArea(trackIndex);
    int trackMidY = trackArea.getY() + trackArea.getHeight() / 2;

    return y < trackMidY;
}

void TrackContentPanel::updateCursorForPosition(int x, int y) {
    // Check track zone first
    bool inUpperZone = isInUpperTrackZone(y);

    if (inUpperZone) {
        // UPPER ZONE: Clip operations
        // Check if over a clip - clip handles its own cursor
        if (getClipComponentAt(x, y) != nullptr) {
            setMouseCursor(juce::MouseCursor::NormalCursor);
            return;
        }
        // Empty space in upper zone - crosshair for marquee selection
        if (isInSelectableArea(x, y)) {
            setMouseCursor(juce::MouseCursor::CrosshairCursor);
        } else {
            setMouseCursor(juce::MouseCursor::NormalCursor);
        }
    } else {
        // LOWER ZONE: Time selection operations
        if (isInSelectableArea(x, y)) {
            if (isOnExistingSelection(x, y)) {
                // Over existing time selection - show grab cursor
                setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            } else {
                // Empty space - I-beam for creating time selection
                setMouseCursor(juce::MouseCursor::IBeamCursor);
            }
        } else {
            setMouseCursor(juce::MouseCursor::NormalCursor);
        }
    }
}

// ============================================================================
// ClipManagerListener Implementation
// ============================================================================

void TrackContentPanel::clipsChanged() {
    rebuildClipComponents();
}

void TrackContentPanel::clipPropertyChanged(ClipId clipId) {
    // Find the clip component and update its position/size
    // Skip if any clip is being dragged to prevent flicker
    for (auto& clipComp : clipComponents_) {
        if (clipComp->getClipId() == clipId) {
            // Check if any clip is currently being dragged
            bool anyDragging = false;
            for (const auto& cc : clipComponents_) {
                if (cc->isCurrentlyDragging()) {
                    anyDragging = true;
                    break;
                }
            }
            if (!anyDragging) {
                updateClipComponentPositions();
            }
            break;
        }
    }
}

void TrackContentPanel::clipSelectionChanged(ClipId /*clipId*/) {
    // Repaint to update selection visuals
    repaint();
}

// ============================================================================
// Clip Management
// ============================================================================

void TrackContentPanel::rebuildClipComponents() {
    // Remove all existing clip components
    clipComponents_.clear();

    // Get all clips
    const auto& clips = ClipManager::getInstance().getClips();

    // Create a component for each clip that belongs to a visible track
    for (const auto& clip : clips) {
        // Check if clip's track is visible
        auto it = std::find(visibleTrackIds_.begin(), visibleTrackIds_.end(), clip.trackId);
        if (it == visibleTrackIds_.end()) {
            continue;  // Track not visible
        }

        auto clipComp = std::make_unique<ClipComponent>(clip.id, this);

        // Set up callbacks
        clipComp->onClipMoved = [](ClipId id, double newStartTime) {
            ClipManager::getInstance().moveClip(id, newStartTime);
        };

        clipComp->onClipMovedToTrack = [](ClipId id, TrackId newTrackId) {
            ClipManager::getInstance().moveClipToTrack(id, newTrackId);
        };

        clipComp->onClipResized = [](ClipId id, double newLength, bool fromStart) {
            ClipManager::getInstance().resizeClip(id, newLength, fromStart);
        };

        clipComp->onClipSelected = [](ClipId id) {
            SelectionManager::getInstance().selectClip(id);
        };

        clipComp->onClipDoubleClicked = [](ClipId /*id*/) {
            // Could open clip in editor, etc.
        };

        // Wire up grid snapping
        clipComp->snapTimeToGrid = snapTimeToGrid;

        addAndMakeVisible(clipComp.get());
        clipComponents_.push_back(std::move(clipComp));
    }

    updateClipComponentPositions();
}

void TrackContentPanel::updateClipComponentPositions() {
    for (auto& clipComp : clipComponents_) {
        const auto* clip = ClipManager::getInstance().getClip(clipComp->getClipId());
        if (!clip) {
            continue;
        }

        // Skip clips that are being dragged - they manage their own position
        if (clipComp->isCurrentlyDragging()) {
            continue;
        }

        // Find the track index
        auto it = std::find(visibleTrackIds_.begin(), visibleTrackIds_.end(), clip->trackId);
        if (it == visibleTrackIds_.end()) {
            clipComp->setVisible(false);
            continue;
        }

        int trackIndex = static_cast<int>(std::distance(visibleTrackIds_.begin(), it));
        auto trackArea = getTrackLaneArea(trackIndex);

        // Calculate clip bounds
        int clipX = timeToPixel(clip->startTime);
        int clipWidth = static_cast<int>(clip->length * currentZoom);

        // Inset from track edges
        int clipY = trackArea.getY() + 2;
        int clipHeight = trackArea.getHeight() - 4;

        clipComp->setBounds(clipX, clipY, juce::jmax(10, clipWidth), clipHeight);
        clipComp->setVisible(true);
    }
}

void TrackContentPanel::createClipFromTimeSelection() {
    if (!timelineController) {
        return;
    }

    const auto& selection = timelineController->getState().selection;
    if (!selection.isActive()) {
        return;
    }

    // Create a clip for each track in the selection
    for (int trackIndex : selection.trackIndices) {
        if (trackIndex >= 0 && trackIndex < static_cast<int>(visibleTrackIds_.size())) {
            TrackId trackId = visibleTrackIds_[trackIndex];
            const auto* track = TrackManager::getInstance().getTrack(trackId);

            if (track) {
                double length = selection.endTime - selection.startTime;

                // Determine clip type based on track type
                if (canContainMIDI(track->type)) {
                    ClipManager::getInstance().createMidiClip(trackId, selection.startTime, length);
                } else if (canContainAudio(track->type)) {
                    ClipManager::getInstance().createAudioClip(trackId, selection.startTime, length,
                                                               "");
                }
            }
        }
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
// Marquee Selection
// ============================================================================

void TrackContentPanel::startMarqueeSelection(const juce::Point<int>& startPoint) {
    isMarqueeActive_ = true;
    marqueeStartPoint_ = startPoint;
    marqueeRect_ = juce::Rectangle<int>(startPoint.x, startPoint.y, 0, 0);
    marqueePreviewClips_.clear();
    currentDragType_ = DragType::Marquee;
}

void TrackContentPanel::updateMarqueeSelection(const juce::Point<int>& currentPoint) {
    if (!isMarqueeActive_) {
        return;
    }

    // Calculate marquee rectangle from start and current point
    int x1 = juce::jmin(marqueeStartPoint_.x, currentPoint.x);
    int y1 = juce::jmin(marqueeStartPoint_.y, currentPoint.y);
    int x2 = juce::jmax(marqueeStartPoint_.x, currentPoint.x);
    int y2 = juce::jmax(marqueeStartPoint_.y, currentPoint.y);

    marqueeRect_ = juce::Rectangle<int>(x1, y1, x2 - x1, y2 - y1);

    // Update highlighted clips
    updateMarqueeHighlights();
    repaint();
}

void TrackContentPanel::finishMarqueeSelection(bool addToSelection) {
    if (!isMarqueeActive_) {
        return;
    }

    isMarqueeActive_ = false;

    // Get all clips in the marquee rectangle
    auto clipsInRect = getClipsInRect(marqueeRect_);

    if (addToSelection) {
        // Add to existing selection (Shift key held)
        for (ClipId clipId : clipsInRect) {
            SelectionManager::getInstance().addClipToSelection(clipId);
        }
    } else {
        // Replace selection
        SelectionManager::getInstance().selectClips(clipsInRect);
    }

    // Clear marquee preview highlights
    for (auto& clipComp : clipComponents_) {
        clipComp->setMarqueeHighlighted(false);
    }
    marqueePreviewClips_.clear();
    marqueeRect_ = juce::Rectangle<int>();

    repaint();
}

std::unordered_set<ClipId> TrackContentPanel::getClipsInRect(
    const juce::Rectangle<int>& rect) const {
    std::unordered_set<ClipId> result;

    for (const auto& clipComp : clipComponents_) {
        if (clipComp->getBounds().intersects(rect)) {
            result.insert(clipComp->getClipId());
        }
    }

    return result;
}

void TrackContentPanel::paintMarqueeRect(juce::Graphics& g) {
    if (!isMarqueeActive_ || marqueeRect_.isEmpty()) {
        return;
    }

    // Semi-transparent white fill
    g.setColour(juce::Colours::white.withAlpha(0.15f));
    g.fillRect(marqueeRect_);

    // White border
    g.setColour(juce::Colours::white.withAlpha(0.8f));
    g.drawRect(marqueeRect_, 1);
}

void TrackContentPanel::updateMarqueeHighlights() {
    auto clipsInRect = getClipsInRect(marqueeRect_);

    // Update clip components
    for (auto& clipComp : clipComponents_) {
        bool inMarquee = clipsInRect.find(clipComp->getClipId()) != clipsInRect.end();
        clipComp->setMarqueeHighlighted(inMarquee);
    }

    marqueePreviewClips_ = clipsInRect;
}

bool TrackContentPanel::checkIfMarqueeNeeded(const juce::Point<int>& currentPoint) const {
    // Create a rectangle from drag start to current point
    int x1 = juce::jmin(mouseDownX, currentPoint.x);
    int y1 = juce::jmin(mouseDownY, currentPoint.y);
    int x2 = juce::jmax(mouseDownX, currentPoint.x);
    int y2 = juce::jmax(mouseDownY, currentPoint.y);

    // Ensure minimum dimensions for intersection check
    // (a zero-height rect won't intersect anything)
    int width = juce::jmax(1, x2 - x1);
    int height = juce::jmax(1, y2 - y1);

    // Expand vertically to cover the track the user clicked in
    // This ensures horizontal drags still detect clips
    int trackIndex = getTrackIndexAtY(mouseDownY);
    if (trackIndex >= 0) {
        auto trackArea = getTrackLaneArea(trackIndex);
        y1 = trackArea.getY();
        height = trackArea.getHeight();
    }

    juce::Rectangle<int> dragRect(x1, y1, width, height);

    // Check if any clips are intersected by the drag rectangle
    for (const auto& clipComp : clipComponents_) {
        if (clipComp->getBounds().intersects(dragRect)) {
            return true;  // Marquee selection needed
        }
    }

    return false;  // Time selection (no clips crossed)
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

    double pixelsPerSecond = currentZoom;
    if (pixelsPerSecond <= 0) {
        return;
    }

    int deltaX = currentPos.x - multiClipDragStartPos_.x;
    double deltaTime = deltaX / pixelsPerSecond;

    // Calculate new anchor time with snapping
    double newAnchorTime = juce::jmax(0.0, multiClipDragStartTime_ + deltaTime);
    if (snapTimeToGrid) {
        double snappedTime = snapTimeToGrid(newAnchorTime);
        // Magnetic snap threshold
        double snapDeltaPixels = std::abs((snappedTime - newAnchorTime) * pixelsPerSecond);
        if (snapDeltaPixels <= 15) {  // SNAP_THRESHOLD_PIXELS
            newAnchorTime = snappedTime;
        }
    }

    double actualDeltaTime = newAnchorTime - multiClipDragStartTime_;

    // Update all clip components visually (don't commit to ClipManager yet)
    for (const auto& dragInfo : multiClipDragInfos_) {
        double newStartTime = juce::jmax(0.0, dragInfo.originalStartTime + actualDeltaTime);

        // Find the clip component
        for (auto& clipComp : clipComponents_) {
            if (clipComp->getClipId() == dragInfo.clipId) {
                const auto* clip = ClipManager::getInstance().getClip(dragInfo.clipId);
                if (clip) {
                    int newX = timeToPixel(newStartTime);
                    int clipWidth = static_cast<int>(clip->length * pixelsPerSecond);
                    clipComp->setBounds(newX, clipComp->getY(), juce::jmax(10, clipWidth),
                                        clipComp->getHeight());
                }
                break;
            }
        }
    }
}

void TrackContentPanel::finishMultiClipDrag() {
    if (!isMovingMultipleClips_ || multiClipDragInfos_.empty()) {
        isMovingMultipleClips_ = false;
        return;
    }

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

        // Apply the move to all selected clips
        for (const auto& dragInfo : multiClipDragInfos_) {
            double newStartTime = juce::jmax(0.0, dragInfo.originalStartTime + actualDeltaTime);
            ClipManager::getInstance().moveClip(dragInfo.clipId, newStartTime);
        }
    }

    // Clean up
    isMovingMultipleClips_ = false;
    anchorClipId_ = INVALID_CLIP_ID;
    multiClipDragInfos_.clear();

    // Refresh positions from ClipManager
    updateClipComponentPositions();
}

void TrackContentPanel::cancelMultiClipDrag() {
    if (!isMovingMultipleClips_) {
        return;
    }

    // Restore original visual positions
    updateClipComponentPositions();

    isMovingMultipleClips_ = false;
    anchorClipId_ = INVALID_CLIP_ID;
    multiClipDragInfos_.clear();
}

// ============================================================================
// Time Selection with Clips
// ============================================================================

void TrackContentPanel::captureClipsInTimeSelection() {
    clipsInTimeSelection_.clear();

    if (!timelineController) {
        return;
    }

    const auto& selection = timelineController->getState().selection;
    if (!selection.isActive()) {
        return;
    }

    // Get all clips and check if they overlap with the time selection
    const auto& clips = ClipManager::getInstance().getClips();

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
                    int clipWidth = static_cast<int>(clip->length * currentZoom);
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

    // Commit all clip moves to ClipManager
    for (const auto& info : clipsInTimeSelection_) {
        double newStartTime = juce::jmax(0.0, info.originalStartTime + deltaTime);
        ClipManager::getInstance().moveClip(info.clipId, newStartTime);
    }

    // Clear the captured clips
    clipsInTimeSelection_.clear();

    // Refresh positions from ClipManager
    updateClipComponentPositions();
}

// ============================================================================
// Keyboard Handling
// ============================================================================

bool TrackContentPanel::keyPressed(const juce::KeyPress& key) {
    auto& selectionManager = SelectionManager::getInstance();

    // Cmd/Ctrl+A: Select all clips
    if (key == juce::KeyPress('a', juce::ModifierKeys::commandModifier, 0)) {
        std::unordered_set<ClipId> allClips;
        for (const auto& clipComp : clipComponents_) {
            allClips.insert(clipComp->getClipId());
        }
        selectionManager.selectClips(allClips);
        return true;
    }

    // Escape: Clear selection
    if (key == juce::KeyPress::escapeKey) {
        selectionManager.clearSelection();
        if (isMarqueeActive_) {
            isMarqueeActive_ = false;
            marqueePreviewClips_.clear();
            for (auto& clipComp : clipComponents_) {
                clipComp->setMarqueeHighlighted(false);
            }
            repaint();
        }
        if (isMovingMultipleClips_) {
            cancelMultiClipDrag();
        }
        return true;
    }

    // Delete/Backspace: Delete selected clips
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        const auto& selectedClips = selectionManager.getSelectedClips();
        if (!selectedClips.empty()) {
            // Copy to vector since we're modifying during iteration
            std::vector<ClipId> clipsToDelete(selectedClips.begin(), selectedClips.end());
            for (ClipId clipId : clipsToDelete) {
                ClipManager::getInstance().deleteClip(clipId);
            }
            selectionManager.clearSelection();
            return true;
        }
    }

    return false;  // Key not handled
}

}  // namespace magica
