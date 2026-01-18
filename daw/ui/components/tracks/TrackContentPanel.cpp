#include "TrackContentPanel.hpp"

#include <iostream>

#include "../../layout/LayoutConfig.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "Config.hpp"

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

    // Build tracks from TrackManager
    tracksChanged();
}

TrackContentPanel::~TrackContentPanel() {
    // Unregister from TrackManager
    TrackManager::getInstance().removeListener(this);

    // Unregister from controller if we have one
    if (timelineController) {
        timelineController->removeListener(this);
    }
}

void TrackContentPanel::tracksChanged() {
    // Rebuild track lanes from TrackManager
    trackLanes.clear();
    selectedTrackIndex = -1;

    const auto& tracks = TrackManager::getInstance().getTracks();
    for (size_t i = 0; i < tracks.size(); ++i) {
        auto lane = std::make_unique<TrackLane>();
        trackLanes.push_back(std::move(lane));
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
    resized();
    repaint();
}

void TrackContentPanel::setVerticalZoom(double zoom) {
    verticalZoom = juce::jlimit(0.5, 3.0, zoom);
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
    if (currentZoom > 0) {
        return static_cast<double>(pixel - LEFT_PADDING) / currentZoom;
    }
    return 0.0;
}

int TrackContentPanel::timeToPixel(double time) const {
    return static_cast<int>(time * currentZoom) + LEFT_PADDING;
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

    // Select track based on click position
    for (size_t i = 0; i < trackLanes.size(); ++i) {
        if (getTrackLaneArea(static_cast<int>(i)).contains(event.getPosition())) {
            selectTrack(static_cast<int>(i));
            break;
        }
    }

    // Check if clicking on an existing selection (to move it)
    if (isOnExistingSelection(event.x, event.y)) {
        const auto& selection = timelineController->getState().selection;
        isMovingSelection = true;
        isCreatingSelection = false;
        moveDragStartTime = pixelToTime(event.x);
        moveSelectionOriginalStart = selection.startTime;
        moveSelectionOriginalEnd = selection.endTime;
        moveSelectionOriginalTracks = selection.trackIndices;
        return;
    }

    // Start time selection tracking if in selectable area
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
        }

        // Clamp to timeline bounds
        double duration = moveSelectionOriginalEnd - moveSelectionOriginalStart;
        if (newStart < 0) {
            newStart = 0;
            newEnd = duration;
        }
        if (newEnd > timelineLength) {
            newEnd = timelineLength;
            newStart = timelineLength - duration;
        }

        // Notify about selection change (preserve original track indices)
        if (onTimeSelectionChanged) {
            onTimeSelectionChanged(newStart, newEnd, moveSelectionOriginalTracks);
        }
    } else if (isCreatingSelection) {
        // Update selection end time
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
        // Finalize move - the selection has already been updated via mouseDrag
        isMovingSelection = false;
        moveDragStartTime = -1.0;
        moveSelectionOriginalStart = -1.0;
        moveSelectionOriginalEnd = -1.0;
        moveSelectionOriginalTracks.clear();
        return;
    }

    if (isCreatingSelection) {
        isCreatingSelection = false;

        // Check if this was a click or a drag using pixel-based threshold
        int deltaX = std::abs(event.x - mouseDownX);
        int deltaY = std::abs(event.y - mouseDownY);

        if (deltaX <= DRAG_THRESHOLD && deltaY <= DRAG_THRESHOLD) {
            // It was a click - schedule playhead move (delayed to allow double-click detection)
            double clickTime = pixelToTime(mouseDownX);
            clickTime = juce::jlimit(0.0, timelineLength, clickTime);

            // Apply snap to grid if callback is set
            if (snapTimeToGrid) {
                clickTime = snapTimeToGrid(clickTime);
            }

            // Schedule playhead change (will be cancelled if double-click detected)
            pendingPlayheadTime = clickTime;
            startTimer(DOUBLE_CLICK_DELAY_MS);
        } else {
            // It was a drag - finalize selection
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
    }
}

void TrackContentPanel::mouseDoubleClick(const juce::MouseEvent& event) {
    // Cancel any pending playhead move (double-click should not move playhead)
    stopTimer();
    pendingPlayheadTime = -1.0;

    // Double-click on empty area clears selection
    if (!isOnExistingSelection(event.x, event.y)) {
        if (onTimeSelectionChanged) {
            // Clear selection by sending invalid values
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
    // Update cursor based on area
    if (isOnExistingSelection(event.x, event.y)) {
        // Show grab cursor when over an existing selection
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    } else if (isInSelectableArea(event.x, event.y)) {
        setMouseCursor(juce::MouseCursor::IBeamCursor);
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

}  // namespace magica
