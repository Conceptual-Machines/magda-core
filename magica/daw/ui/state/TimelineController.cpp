#include "TimelineController.hpp"

#include <algorithm>
#include <iostream>

#include "Config.hpp"

namespace magica {

TimelineController::TimelineController() {
    // Load configuration values
    auto& config = magica::Config::getInstance();
    state.timelineLength = config.getDefaultTimelineLength();

    // Set default zoom to show a reasonable view duration
    double defaultViewDuration = config.getDefaultZoomViewDuration();
    if (defaultViewDuration > 0 && state.zoom.viewportWidth > 0) {
        state.zoom.horizontalZoom = state.zoom.viewportWidth / defaultViewDuration;
    }

    std::cout << "TimelineController: initialized with timelineLength=" << state.timelineLength
              << std::endl;
}

TimelineController::~TimelineController() = default;

void TimelineController::dispatch(const TimelineEvent& event) {
    // Determine if this event should create an undo point
    // We push undo state for significant changes but not for continuous operations
    bool shouldPushUndo = std::visit(
        [](const auto& e) -> bool {
            using T = std::decay_t<decltype(e)>;
            // Events that create undo points (discrete, significant changes)
            if constexpr (std::is_same_v<T, SetLoopRegionEvent> ||
                          std::is_same_v<T, ClearLoopRegionEvent> ||
                          std::is_same_v<T, CreateLoopFromSelectionEvent> ||
                          std::is_same_v<T, ZoomToFitEvent> || std::is_same_v<T, ResetZoomEvent> ||
                          std::is_same_v<T, AddSectionEvent> ||
                          std::is_same_v<T, RemoveSectionEvent> ||
                          std::is_same_v<T, MoveSectionEvent> ||
                          std::is_same_v<T, ResizeSectionEvent> ||
                          std::is_same_v<T, SetTimelineLengthEvent>) {
                return true;
            }
            // Events that don't create undo points (continuous or minor changes)
            return false;
        },
        event);

    // Push undo state if needed
    if (shouldPushUndo) {
        pushUndoState();
    }

    // Process the event using std::visit
    ChangeFlags changes =
        std::visit([this](const auto& e) -> ChangeFlags { return this->handleEvent(e); }, event);

    // Notify listeners if anything changed
    if (changes != ChangeFlags::None) {
        notifyListeners(changes);
    }
}

void TimelineController::addListener(TimelineStateListener* listener) {
    if (listener && std::find(listeners.begin(), listeners.end(), listener) == listeners.end()) {
        listeners.push_back(listener);
    }
}

void TimelineController::removeListener(TimelineStateListener* listener) {
    listeners.erase(std::remove(listeners.begin(), listeners.end(), listener), listeners.end());
}

void TimelineController::pushUndoState() {
    undoStack.push_back(state);

    // Limit undo stack size
    while (undoStack.size() > maxUndoStates) {
        undoStack.pop_front();
    }

    // Clear redo stack when new action is taken
    redoStack.clear();
}

bool TimelineController::undo() {
    if (undoStack.empty()) {
        return false;
    }

    // Push current state to redo stack
    redoStack.push_back(state);

    // Restore previous state
    state = undoStack.back();
    undoStack.pop_back();

    // Notify all listeners
    notifyListeners(ChangeFlags::All);

    return true;
}

bool TimelineController::redo() {
    if (redoStack.empty()) {
        return false;
    }

    // Push current state to undo stack
    undoStack.push_back(state);

    // Restore next state
    state = redoStack.back();
    redoStack.pop_back();

    // Notify all listeners
    notifyListeners(ChangeFlags::All);

    return true;
}

void TimelineController::clearUndoHistory() {
    undoStack.clear();
    redoStack.clear();
}

// ===== Zoom Event Handlers =====

TimelineController::ChangeFlags TimelineController::handleEvent(const SetZoomEvent& e) {
    double newZoom = clampZoom(e.zoom);
    if (newZoom == state.zoom.horizontalZoom) {
        return ChangeFlags::None;
    }

    state.zoom.horizontalZoom = newZoom;
    clampScrollPosition();

    return ChangeFlags::Zoom | ChangeFlags::Scroll;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const SetZoomCenteredEvent& e) {
    double newZoom = clampZoom(e.zoom);

    // Calculate where the center time should appear after zoom
    int viewportCenter = state.zoom.viewportWidth / 2;
    int timeContentX = static_cast<int>(e.centerTime * newZoom) + TimelineState::LEFT_PADDING;
    int newScrollX = timeContentX - viewportCenter;

    state.zoom.horizontalZoom = newZoom;
    state.zoom.scrollX = newScrollX;
    clampScrollPosition();

    return ChangeFlags::Zoom | ChangeFlags::Scroll;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const SetZoomAnchoredEvent& e) {
    double newZoom = clampZoom(e.zoom);

    // Calculate scroll position to keep anchorTime at anchorScreenX
    int anchorPixelPos = static_cast<int>(e.anchorTime * newZoom) + TimelineState::LEFT_PADDING;
    int newScrollX = anchorPixelPos - e.anchorScreenX;

    state.zoom.horizontalZoom = newZoom;
    state.zoom.scrollX = newScrollX;
    clampScrollPosition();

    return ChangeFlags::Zoom | ChangeFlags::Scroll;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const ZoomToFitEvent& e) {
    if (e.endTime <= e.startTime) {
        return ChangeFlags::None;
    }

    double duration = e.endTime - e.startTime;
    double padding = duration * e.paddingPercent;
    double zoomToFit = static_cast<double>(state.zoom.viewportWidth) / (duration + padding * 2);

    state.zoom.horizontalZoom = clampZoom(zoomToFit);

    // Calculate scroll to show the start (with padding)
    int scrollX = static_cast<int>((e.startTime - padding) * state.zoom.horizontalZoom);
    state.zoom.scrollX = juce::jmax(0, scrollX);
    clampScrollPosition();

    return ChangeFlags::Zoom | ChangeFlags::Scroll;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const ResetZoomEvent& /*e*/) {
    if (state.timelineLength <= 0 || state.zoom.viewportWidth <= 0) {
        return ChangeFlags::None;
    }

    int availableWidth = state.zoom.viewportWidth - TimelineState::LEFT_PADDING;
    double fitZoom = static_cast<double>(availableWidth) / state.timelineLength;

    state.zoom.horizontalZoom = clampZoom(fitZoom);
    state.zoom.scrollX = 0;

    return ChangeFlags::Zoom | ChangeFlags::Scroll;
}

// ===== Scroll Event Handlers =====

TimelineController::ChangeFlags TimelineController::handleEvent(const SetScrollPositionEvent& e) {
    bool changed = false;

    if (e.scrollX != state.zoom.scrollX) {
        state.zoom.scrollX = e.scrollX;
        changed = true;
    }

    if (e.scrollY >= 0 && e.scrollY != state.zoom.scrollY) {
        state.zoom.scrollY = e.scrollY;
        changed = true;
    }

    if (changed) {
        clampScrollPosition();
        return ChangeFlags::Scroll;
    }

    return ChangeFlags::None;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const ScrollByDeltaEvent& e) {
    state.zoom.scrollX += e.deltaX;
    state.zoom.scrollY += e.deltaY;
    clampScrollPosition();

    return ChangeFlags::Scroll;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const ScrollToTimeEvent& e) {
    int targetX =
        static_cast<int>(e.time * state.zoom.horizontalZoom) + TimelineState::LEFT_PADDING;

    if (e.center) {
        targetX -= state.zoom.viewportWidth / 2;
    }

    state.zoom.scrollX = targetX;
    clampScrollPosition();

    return ChangeFlags::Scroll;
}

// ===== Playhead Event Handlers =====

TimelineController::ChangeFlags TimelineController::handleEvent(const SetEditPositionEvent& e) {
    double newPos = juce::jlimit(0.0, state.timelineLength, e.position);
    if (newPos == state.playhead.editPosition) {
        return ChangeFlags::None;
    }

    state.playhead.editPosition = newPos;
    // If not playing, also sync playbackPosition to editPosition
    if (!state.playhead.isPlaying) {
        state.playhead.playbackPosition = newPos;
    }
    return ChangeFlags::Playhead;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const SetPlayheadPositionEvent& e) {
    // Backwards compatible: SetPlayheadPositionEvent behaves like SetEditPositionEvent
    return handleEvent(SetEditPositionEvent{e.position});
}

TimelineController::ChangeFlags TimelineController::handleEvent(const SetPlaybackPositionEvent& e) {
    // Only updates the playback position (the moving cursor), not the edit position
    double newPos = juce::jlimit(0.0, state.timelineLength, e.position);
    if (newPos == state.playhead.playbackPosition) {
        return ChangeFlags::None;
    }

    state.playhead.playbackPosition = newPos;
    return ChangeFlags::Playhead;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const StartPlaybackEvent& /*e*/) {
    if (state.playhead.isPlaying) {
        return ChangeFlags::None;  // Already playing
    }

    state.playhead.isPlaying = true;
    // Sync playbackPosition to editPosition at start of playback
    state.playhead.playbackPosition = state.playhead.editPosition;
    return ChangeFlags::Playhead;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const StopPlaybackEvent& /*e*/) {
    if (!state.playhead.isPlaying) {
        return ChangeFlags::None;  // Already stopped
    }

    state.playhead.isPlaying = false;
    state.playhead.isRecording = false;
    // Reset playbackPosition to editPosition (Bitwig behavior)
    state.playhead.playbackPosition = state.playhead.editPosition;
    return ChangeFlags::Playhead;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const MovePlayheadByDeltaEvent& e) {
    double newPos =
        juce::jlimit(0.0, state.timelineLength, state.playhead.editPosition + e.deltaSeconds);
    if (newPos == state.playhead.editPosition) {
        return ChangeFlags::None;
    }

    state.playhead.editPosition = newPos;
    // If not playing, also sync playbackPosition
    if (!state.playhead.isPlaying) {
        state.playhead.playbackPosition = newPos;
    }
    return ChangeFlags::Playhead;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const SetPlaybackStateEvent& e) {
    bool changed = false;

    if (state.playhead.isPlaying != e.isPlaying) {
        state.playhead.isPlaying = e.isPlaying;
        // If starting playback, sync playbackPosition to editPosition
        if (e.isPlaying) {
            state.playhead.playbackPosition = state.playhead.editPosition;
        } else {
            // If stopping, reset playbackPosition to editPosition
            state.playhead.playbackPosition = state.playhead.editPosition;
        }
        changed = true;
    }

    if (state.playhead.isRecording != e.isRecording) {
        state.playhead.isRecording = e.isRecording;
        changed = true;
    }

    return changed ? ChangeFlags::Playhead : ChangeFlags::None;
}

// ===== Selection Event Handlers =====

TimelineController::ChangeFlags TimelineController::handleEvent(const SetTimeSelectionEvent& e) {
    double start = juce::jlimit(0.0, state.timelineLength, e.startTime);
    double end = juce::jlimit(0.0, state.timelineLength, e.endTime);

    // Ensure start < end
    if (start > end) {
        std::swap(start, end);
    }

    state.selection.startTime = start;
    state.selection.endTime = end;
    state.selection.trackIndices = e.trackIndices;
    state.selection.visuallyHidden = false;  // New selection is always visible

    return ChangeFlags::Selection;
}

TimelineController::ChangeFlags TimelineController::handleEvent(
    const ClearTimeSelectionEvent& /*e*/) {
    if (!state.selection.isActive()) {
        return ChangeFlags::None;
    }

    state.selection.clear();
    return ChangeFlags::Selection;
}

TimelineController::ChangeFlags TimelineController::handleEvent(
    const CreateLoopFromSelectionEvent& /*e*/) {
    if (!state.selection.isActive()) {
        return ChangeFlags::None;
    }

    state.loop.startTime = state.selection.startTime;
    state.loop.endTime = state.selection.endTime;
    state.loop.enabled = true;

    // Hide selection visually but keep data for transport display
    state.selection.hideVisually();

    return ChangeFlags::Selection | ChangeFlags::Loop;
}

// ===== Loop Event Handlers =====

TimelineController::ChangeFlags TimelineController::handleEvent(const SetLoopRegionEvent& e) {
    double start = juce::jlimit(0.0, state.timelineLength, e.startTime);
    double end = juce::jlimit(0.0, state.timelineLength, e.endTime);

    // Ensure minimum duration
    if (end - start < 0.01) {
        end = start + 0.01;
    }

    state.loop.startTime = start;
    state.loop.endTime = end;

    // Enable loop if it wasn't valid before
    if (!state.loop.enabled && state.loop.isValid()) {
        state.loop.enabled = true;
    }

    return ChangeFlags::Loop;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const ClearLoopRegionEvent& /*e*/) {
    if (!state.loop.isValid()) {
        return ChangeFlags::None;
    }

    state.loop.clear();
    return ChangeFlags::Loop;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const SetLoopEnabledEvent& e) {
    if (!state.loop.isValid()) {
        return ChangeFlags::None;
    }

    if (state.loop.enabled == e.enabled) {
        return ChangeFlags::None;
    }

    state.loop.enabled = e.enabled;
    return ChangeFlags::Loop;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const MoveLoopRegionEvent& e) {
    if (!state.loop.isValid()) {
        return ChangeFlags::None;
    }

    double duration = state.loop.getDuration();
    double newStart = state.loop.startTime + e.deltaSeconds;

    // Clamp to valid range
    newStart = juce::jlimit(0.0, state.timelineLength - duration, newStart);

    state.loop.startTime = newStart;
    state.loop.endTime = newStart + duration;

    return ChangeFlags::Loop;
}

// ===== Tempo Event Handlers =====

TimelineController::ChangeFlags TimelineController::handleEvent(const SetTempoEvent& e) {
    double newBpm = juce::jlimit(20.0, 999.0, e.bpm);
    if (newBpm == state.tempo.bpm) {
        return ChangeFlags::None;
    }

    state.tempo.bpm = newBpm;
    return ChangeFlags::Tempo;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const SetTimeSignatureEvent& e) {
    int num = juce::jlimit(1, 16, e.numerator);
    int den = juce::jlimit(1, 16, e.denominator);

    if (num == state.tempo.timeSignatureNumerator && den == state.tempo.timeSignatureDenominator) {
        return ChangeFlags::None;
    }

    state.tempo.timeSignatureNumerator = num;
    state.tempo.timeSignatureDenominator = den;
    return ChangeFlags::Tempo;
}

// ===== Display Event Handlers =====

TimelineController::ChangeFlags TimelineController::handleEvent(const SetTimeDisplayModeEvent& e) {
    if (state.display.timeDisplayMode == e.mode) {
        return ChangeFlags::None;
    }

    state.display.timeDisplayMode = e.mode;
    return ChangeFlags::Display;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const SetSnapEnabledEvent& e) {
    if (state.display.snapEnabled == e.enabled) {
        return ChangeFlags::None;
    }

    state.display.snapEnabled = e.enabled;
    return ChangeFlags::Display;
}

TimelineController::ChangeFlags TimelineController::handleEvent(
    const SetArrangementLockedEvent& e) {
    if (state.display.arrangementLocked == e.locked) {
        return ChangeFlags::None;
    }

    state.display.arrangementLocked = e.locked;
    return ChangeFlags::Display;
}

// ===== Section Event Handlers =====

TimelineController::ChangeFlags TimelineController::handleEvent(const AddSectionEvent& e) {
    state.sections.emplace_back(e.startTime, e.endTime, e.name, e.colour);
    return ChangeFlags::Sections;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const RemoveSectionEvent& e) {
    if (e.index < 0 || e.index >= static_cast<int>(state.sections.size())) {
        return ChangeFlags::None;
    }

    state.sections.erase(state.sections.begin() + e.index);

    // Update selected index
    if (state.selectedSectionIndex == e.index) {
        state.selectedSectionIndex = -1;
    } else if (state.selectedSectionIndex > e.index) {
        state.selectedSectionIndex--;
    }

    return ChangeFlags::Sections;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const MoveSectionEvent& e) {
    if (e.index < 0 || e.index >= static_cast<int>(state.sections.size())) {
        return ChangeFlags::None;
    }

    auto& section = state.sections[e.index];
    double duration = section.getDuration();
    double newStart = juce::jmax(0.0, e.newStartTime);
    double newEnd = juce::jmin(state.timelineLength, newStart + duration);

    section.startTime = newStart;
    section.endTime = newEnd;

    return ChangeFlags::Sections;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const ResizeSectionEvent& e) {
    if (e.index < 0 || e.index >= static_cast<int>(state.sections.size())) {
        return ChangeFlags::None;
    }

    auto& section = state.sections[e.index];

    double start = juce::jlimit(0.0, state.timelineLength, e.newStartTime);
    double end = juce::jlimit(0.0, state.timelineLength, e.newEndTime);

    // Ensure minimum duration
    if (end - start < 1.0) {
        if (e.newStartTime != section.startTime) {
            start = juce::jmin(start, section.endTime - 1.0);
        } else {
            end = juce::jmax(end, section.startTime + 1.0);
        }
    }

    section.startTime = start;
    section.endTime = end;

    return ChangeFlags::Sections;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const SelectSectionEvent& e) {
    if (state.selectedSectionIndex == e.index) {
        return ChangeFlags::None;
    }

    state.selectedSectionIndex = e.index;
    return ChangeFlags::Sections;
}

// ===== Viewport Event Handlers =====

TimelineController::ChangeFlags TimelineController::handleEvent(const ViewportResizedEvent& e) {
    bool changed = false;

    if (e.width != state.zoom.viewportWidth) {
        state.zoom.viewportWidth = e.width;
        changed = true;
    }

    if (e.height != state.zoom.viewportHeight) {
        state.zoom.viewportHeight = e.height;
        changed = true;
    }

    if (changed) {
        clampScrollPosition();
        return ChangeFlags::Zoom | ChangeFlags::Scroll;
    }

    return ChangeFlags::None;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const SetTimelineLengthEvent& e) {
    if (e.lengthInSeconds == state.timelineLength) {
        return ChangeFlags::None;
    }

    state.timelineLength = e.lengthInSeconds;

    // Clamp playhead positions to new length
    state.playhead.editPosition = juce::jmin(state.playhead.editPosition, state.timelineLength);
    state.playhead.playbackPosition =
        juce::jmin(state.playhead.playbackPosition, state.timelineLength);

    if (state.loop.isValid()) {
        state.loop.endTime = juce::jmin(state.loop.endTime, state.timelineLength);
        if (state.loop.startTime >= state.loop.endTime) {
            state.loop.clear();
        }
    }

    clampScrollPosition();

    return ChangeFlags::Timeline | ChangeFlags::Zoom | ChangeFlags::Scroll;
}

// ===== Notification Helpers =====

void TimelineController::notifyListeners(ChangeFlags changes) {
    for (auto* listener : listeners) {
        // Call specific handlers first
        if (hasFlag(changes, ChangeFlags::Zoom) || hasFlag(changes, ChangeFlags::Scroll)) {
            listener->zoomStateChanged(state);
        }
        if (hasFlag(changes, ChangeFlags::Playhead)) {
            listener->playheadStateChanged(state);
        }
        if (hasFlag(changes, ChangeFlags::Selection)) {
            listener->selectionStateChanged(state);
        }
        if (hasFlag(changes, ChangeFlags::Loop)) {
            listener->loopStateChanged(state);
        }
        if (hasFlag(changes, ChangeFlags::Tempo)) {
            listener->tempoStateChanged(state);
        }
        if (hasFlag(changes, ChangeFlags::Display)) {
            listener->displayConfigChanged(state);
        }

        // Always call the general handler
        listener->timelineStateChanged(state);
    }
}

// ===== Helper Methods =====

void TimelineController::clampScrollPosition() {
    int maxX = state.getMaxScrollX();
    state.zoom.scrollX = juce::jlimit(0, maxX, state.zoom.scrollX);
    state.zoom.scrollY = juce::jmax(0, state.zoom.scrollY);
}

double TimelineController::clampZoom(double zoom) const {
    auto& config = magica::Config::getInstance();
    double minZoom = state.getMinZoom();
    minZoom = juce::jmax(minZoom, config.getMinZoomLevel());
    double maxZoom = config.getMaxZoomLevel();

    return juce::jlimit(minZoom, maxZoom, zoom);
}

}  // namespace magica
