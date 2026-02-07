#include "TimelineController.hpp"

#include <algorithm>
#include <iostream>

#include "../../core/ClipManager.hpp"
#include "../utils/TimelineUtils.hpp"
#include "Config.hpp"

namespace magda {

TimelineController::TimelineController() {
    // Set as current instance for global access
    currentInstance_ = this;

    // Load configuration values
    auto& config = magda::Config::getInstance();
    state.timelineLength = config.getDefaultTimelineLength();

    // Set default zoom (ppb) to show a reasonable view duration
    double defaultViewDuration = config.getDefaultZoomViewDuration();
    if (defaultViewDuration > 0 && state.zoom.viewportWidth > 0) {
        double beats = state.secondsToBeats(defaultViewDuration);
        if (beats > 0)
            state.zoom.horizontalZoom = state.zoom.viewportWidth / beats;
    }

    std::cout << "TimelineController: initialized with timelineLength=" << state.timelineLength
              << std::endl;
}

TimelineController::~TimelineController() {
    // Clear the current instance
    if (currentInstance_ == this) {
        currentInstance_ = nullptr;
    }
}

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
                          std::is_same_v<T, SetPunchRegionEvent> ||
                          std::is_same_v<T, ClearPunchRegionEvent> ||
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

void TimelineController::addAudioEngineListener(AudioEngineListener* listener) {
    if (listener && std::find(audioEngineListeners.begin(), audioEngineListeners.end(), listener) ==
                        audioEngineListeners.end()) {
        audioEngineListeners.push_back(listener);
    }
}

void TimelineController::removeAudioEngineListener(AudioEngineListener* listener) {
    audioEngineListeners.erase(
        std::remove(audioEngineListeners.begin(), audioEngineListeners.end(), listener),
        audioEngineListeners.end());
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
    // Convert time to beats since zoom is ppb
    int viewportCenter = state.zoom.viewportWidth / 2;
    double centerBeats = state.secondsToBeats(e.centerTime);
    int timeContentX = static_cast<int>(centerBeats * newZoom) + TimelineState::LEFT_PADDING;
    int newScrollX = timeContentX - viewportCenter;

    state.zoom.horizontalZoom = newZoom;
    state.zoom.scrollX = newScrollX;
    clampScrollPosition();

    return ChangeFlags::Zoom | ChangeFlags::Scroll;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const SetZoomAnchoredEvent& e) {
    double newZoom = clampZoom(e.zoom);

    // Calculate scroll position to keep anchorTime at anchorScreenX
    // Convert time to beats since zoom is ppb
    double anchorBeats = state.secondsToBeats(e.anchorTime);
    int anchorPixelPos = static_cast<int>(anchorBeats * newZoom) + TimelineState::LEFT_PADDING;
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

    // Convert duration to beats since zoom is ppb
    double duration = e.endTime - e.startTime;
    double durationBeats = state.secondsToBeats(duration);
    double paddingBeats = durationBeats * e.paddingPercent;
    double zoomToFit =
        static_cast<double>(state.zoom.viewportWidth) / (durationBeats + paddingBeats * 2);

    state.zoom.horizontalZoom = clampZoom(zoomToFit);

    // Calculate scroll to show the start (with padding)
    double startBeats = state.secondsToBeats(e.startTime) - paddingBeats;
    int scrollX = static_cast<int>(startBeats * state.zoom.horizontalZoom);
    state.zoom.scrollX = juce::jmax(0, scrollX);
    clampScrollPosition();

    return ChangeFlags::Zoom | ChangeFlags::Scroll;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const ResetZoomEvent& /*e*/) {
    if (state.timelineLength <= 0 || state.zoom.viewportWidth <= 0) {
        return ChangeFlags::None;
    }

    // Convert timeline length to beats since zoom is ppb
    int availableWidth = state.zoom.viewportWidth - TimelineState::LEFT_PADDING;
    double beats = state.secondsToBeats(state.timelineLength);
    double fitZoom = (beats > 0) ? static_cast<double>(availableWidth) / beats : 1.0;

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
    // Convert time to beats since zoom is ppb
    double beats = state.secondsToBeats(e.time);
    int targetX = static_cast<int>(beats * state.zoom.horizontalZoom) + TimelineState::LEFT_PADDING;

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
    state.playhead.editPositionBeats =
        magda::TimelineUtils::secondsToBeats(newPos, state.tempo.bpm);
    // If not playing, also sync playbackPosition to editPosition
    if (!state.playhead.isPlaying) {
        state.playhead.playbackPosition = newPos;
    }

    // Notify transport listeners of edit position change
    for (auto* listener : audioEngineListeners) {
        listener->onEditPositionChanged(newPos);
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

    // Notify transport listeners to start playback
    for (auto* listener : audioEngineListeners) {
        listener->onTransportPlay(state.playhead.editPosition);
    }

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

    // Notify transport listeners to stop playback
    for (auto* listener : audioEngineListeners) {
        listener->onTransportStop(state.playhead.editPosition);
    }

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

TimelineController::ChangeFlags TimelineController::handleEvent(const SetEditCursorEvent& e) {
    double newPos = e.position;

    // Allow -1.0 to hide the cursor, otherwise clamp to valid range
    if (newPos >= 0.0) {
        newPos = juce::jlimit(0.0, state.timelineLength, newPos);
    }

    if (newPos == state.editCursorPosition) {
        return ChangeFlags::None;
    }

    state.editCursorPosition = newPos;
    // Use Selection flag since edit cursor is an editing-related visual
    return ChangeFlags::Selection;
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
    state.selection.startBeats = magda::TimelineUtils::secondsToBeats(start, state.tempo.bpm);
    state.selection.endBeats = magda::TimelineUtils::secondsToBeats(end, state.tempo.bpm);
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

    // Notify audio engine of loop region change
    for (auto* listener : audioEngineListeners) {
        listener->onLoopRegionChanged(state.loop.startTime, state.loop.endTime, true);
    }

    return ChangeFlags::Selection | ChangeFlags::Loop;
}

// ===== Loop Event Handlers =====

TimelineController::ChangeFlags TimelineController::handleEvent(const SetLoopRegionEvent& e) {
    double start = juce::jlimit(0.0, state.timelineLength, e.startTime);
    double end = juce::jlimit(0.0, state.timelineLength, e.endTime);

    std::cout << "[SetLoopRegion] Input: startTime=" << e.startTime << "s, endTime=" << e.endTime
              << "s" << std::endl;

    // Ensure minimum duration
    if (end - start < 0.01) {
        end = start + 0.01;
    }

    state.loop.startTime = start;
    state.loop.endTime = end;

    // Store beat positions (authoritative for tempo changes)
    double bpm = state.tempo.bpm;
    state.loop.startBeats = magda::TimelineUtils::secondsToBeats(start, bpm);
    state.loop.endBeats = magda::TimelineUtils::secondsToBeats(end, bpm);

    // Enable loop if it wasn't valid before
    if (!state.loop.enabled && state.loop.isValid()) {
        state.loop.enabled = true;
    }

    // Notify audio engine of loop region change
    for (auto* listener : audioEngineListeners) {
        listener->onLoopRegionChanged(start, end, state.loop.enabled);
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

    // Notify audio engine of loop enabled change
    for (auto* listener : audioEngineListeners) {
        listener->onLoopEnabledChanged(e.enabled);
    }

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

// ===== Punch In/Out Event Handlers =====

TimelineController::ChangeFlags TimelineController::handleEvent(const SetPunchRegionEvent& e) {
    double start = juce::jlimit(0.0, state.timelineLength, e.startTime);
    double end = juce::jlimit(0.0, state.timelineLength, e.endTime);

    // Ensure minimum duration
    if (end - start < 0.01) {
        end = start + 0.01;
    }

    state.punch.startTime = start;
    state.punch.endTime = end;

    // Store beat positions (authoritative for tempo changes)
    double bpm = state.tempo.bpm;
    state.punch.startBeats = magda::TimelineUtils::secondsToBeats(start, bpm);
    state.punch.endBeats = magda::TimelineUtils::secondsToBeats(end, bpm);

    // Enable both punch in/out if region wasn't valid before
    if (!state.punch.isEnabled() && state.punch.isValid()) {
        state.punch.punchInEnabled = true;
        state.punch.punchOutEnabled = true;
    }

    // Notify audio engine of punch region change
    for (auto* listener : audioEngineListeners) {
        listener->onPunchRegionChanged(start, end, state.punch.punchInEnabled,
                                       state.punch.punchOutEnabled);
    }

    return ChangeFlags::Punch;
}

TimelineController::ChangeFlags TimelineController::handleEvent(
    const ClearPunchRegionEvent& /*e*/) {
    if (!state.punch.isValid()) {
        return ChangeFlags::None;
    }

    state.punch.clear();

    // Notify audio engine
    for (auto* listener : audioEngineListeners) {
        listener->onPunchRegionChanged(-1.0, -1.0, false, false);
    }

    return ChangeFlags::Punch;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const SetPunchInEnabledEvent& e) {
    if (!state.punch.isValid()) {
        return ChangeFlags::None;
    }

    if (state.punch.punchInEnabled == e.enabled) {
        return ChangeFlags::None;
    }

    state.punch.punchInEnabled = e.enabled;

    // Notify audio engine of punch enabled change
    for (auto* listener : audioEngineListeners) {
        listener->onPunchEnabledChanged(state.punch.punchInEnabled, state.punch.punchOutEnabled);
    }

    return ChangeFlags::Punch;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const SetPunchOutEnabledEvent& e) {
    if (!state.punch.isValid()) {
        return ChangeFlags::None;
    }

    if (state.punch.punchOutEnabled == e.enabled) {
        return ChangeFlags::None;
    }

    state.punch.punchOutEnabled = e.enabled;

    // Notify audio engine of punch enabled change
    for (auto* listener : audioEngineListeners) {
        listener->onPunchEnabledChanged(state.punch.punchInEnabled, state.punch.punchOutEnabled);
    }

    return ChangeFlags::Punch;
}

// ===== Tempo Event Handlers =====

TimelineController::ChangeFlags TimelineController::handleEvent(const SetTempoEvent& e) {
    double newBpm = juce::jlimit(20.0, 999.0, e.bpm);
    if (newBpm == state.tempo.bpm) {
        return ChangeFlags::None;
    }

    double oldBpm = state.tempo.bpm;
    state.tempo.bpm = newBpm;

    // Update all beat-anchored positions to maintain bar/beat positions
    uint32_t extraFlags = 0;

    // --- Edit cursor ---
    if (state.playhead.editPosition > 0.0) {
        // Migration: calculate beat position if it was never set
        if (state.playhead.editPositionBeats <= 0.0 && state.playhead.editPosition > 0.0) {
            state.playhead.editPositionBeats =
                magda::TimelineUtils::secondsToBeats(state.playhead.editPosition, oldBpm);
        }
        state.playhead.editPosition =
            magda::TimelineUtils::beatsToSeconds(state.playhead.editPositionBeats, newBpm);
        if (!state.playhead.isPlaying) {
            state.playhead.playbackPosition = state.playhead.editPosition;
        }
        extraFlags |= static_cast<uint32_t>(ChangeFlags::Playhead);
    }

    // --- Time selection ---
    if (state.selection.isActive()) {
        // Migration: calculate beat positions if missing
        if (state.selection.startBeats < 0 && state.selection.endBeats < 0) {
            state.selection.startBeats =
                magda::TimelineUtils::secondsToBeats(state.selection.startTime, oldBpm);
            state.selection.endBeats =
                magda::TimelineUtils::secondsToBeats(state.selection.endTime, oldBpm);
        }
        if (state.selection.startBeats >= 0 && state.selection.endBeats >= 0) {
            state.selection.startTime =
                magda::TimelineUtils::beatsToSeconds(state.selection.startBeats, newBpm);
            state.selection.endTime =
                magda::TimelineUtils::beatsToSeconds(state.selection.endBeats, newBpm);
            extraFlags |= static_cast<uint32_t>(ChangeFlags::Selection);
        }
    }

    // --- Punch region ---
    if (state.punch.isValid() && state.punch.startBeats < 0 && state.punch.endBeats < 0) {
        state.punch.startBeats =
            magda::TimelineUtils::secondsToBeats(state.punch.startTime, oldBpm);
        state.punch.endBeats = magda::TimelineUtils::secondsToBeats(state.punch.endTime, oldBpm);
    }

    if (state.punch.isValid() && state.punch.startBeats >= 0 && state.punch.endBeats >= 0) {
        state.punch.startTime =
            magda::TimelineUtils::beatsToSeconds(state.punch.startBeats, newBpm);
        state.punch.endTime = magda::TimelineUtils::beatsToSeconds(state.punch.endBeats, newBpm);
        extraFlags |= static_cast<uint32_t>(ChangeFlags::Punch);
    }

    // --- Loop region ---
    // Migration: Calculate beat positions if missing (e.g., loop created without going through
    // SetLoopRegionEvent)
    if (state.loop.isValid() && state.loop.startBeats < 0 && state.loop.endBeats < 0) {
        state.loop.startBeats = magda::TimelineUtils::secondsToBeats(state.loop.startTime, oldBpm);
        state.loop.endBeats = magda::TimelineUtils::secondsToBeats(state.loop.endTime, oldBpm);
    }

    if (state.loop.isValid() && state.loop.startBeats >= 0 && state.loop.endBeats >= 0) {
        state.loop.startTime = magda::TimelineUtils::beatsToSeconds(state.loop.startBeats, newBpm);
        state.loop.endTime = magda::TimelineUtils::beatsToSeconds(state.loop.endBeats, newBpm);
        extraFlags |= static_cast<uint32_t>(ChangeFlags::Loop);
    }

    // IMPORTANT: Notify audio engine FIRST so TE's tempo sequence is updated
    // before we sync clips (clips will read BPM from TE's tempo sequence)
    for (auto* listener : audioEngineListeners) {
        listener->onTempoChanged(newBpm);
    }

    // Notify audio engine of updated loop region (TE transport needs new time positions)
    if (state.loop.isValid() && state.loop.enabled) {
        for (auto* listener : audioEngineListeners) {
            listener->onLoopRegionChanged(state.loop.startTime, state.loop.endTime, true);
        }
    }

    // Notify audio engine of updated punch region
    if (state.punch.isValid() && state.punch.isEnabled()) {
        for (auto* listener : audioEngineListeners) {
            listener->onPunchRegionChanged(state.punch.startTime, state.punch.endTime,
                                           state.punch.punchInEnabled, state.punch.punchOutEnabled);
        }
    }

    // Update auto-tempo clips when tempo changes
    // Beats are authoritative — update derived seconds and notify AudioBridge to re-sync TE.
    // UI reads beats directly so the notification doesn't cause stale-BPM layout issues.
    if (std::abs(newBpm - oldBpm) > 0.01) {
        auto& clipManager = ClipManager::getInstance();
        auto allClips = clipManager.getClips();

        // First pass: update all seconds from beats
        std::vector<ClipId> updatedClipIds;
        for (const auto& clip : allClips) {
            if (clip.autoTempo && clip.type == ClipType::Audio) {
                auto* mutableClip = clipManager.getClip(clip.id);
                if (!mutableClip) {
                    continue;
                }

                // Migration: populate beat values if not set
                if (mutableClip->startBeats < 0) {
                    mutableClip->startBeats =
                        magda::TimelineUtils::secondsToBeats(clip.startTime, oldBpm);
                }
                if (mutableClip->lengthBeats <= 0.0) {
                    mutableClip->lengthBeats =
                        magda::TimelineUtils::secondsToBeats(clip.length, oldBpm);
                }

                // Update derived seconds
                mutableClip->startTime =
                    magda::TimelineUtils::beatsToSeconds(mutableClip->startBeats, newBpm);
                mutableClip->length =
                    magda::TimelineUtils::beatsToSeconds(mutableClip->lengthBeats, newBpm);

                updatedClipIds.push_back(clip.id);
            }
        }

        // Second pass: notify so AudioBridge re-syncs TE clip positions
        for (auto clipId : updatedClipIds) {
            clipManager.forceNotifyClipPropertyChanged(clipId);
        }
    }

    // Return combined flags for all updated state
    return static_cast<ChangeFlags>(static_cast<uint32_t>(ChangeFlags::Tempo) | extraFlags);
}

TimelineController::ChangeFlags TimelineController::handleEvent(const SetTimeSignatureEvent& e) {
    int num = juce::jlimit(1, 16, e.numerator);
    int den = juce::jlimit(1, 16, e.denominator);

    if (num == state.tempo.timeSignatureNumerator && den == state.tempo.timeSignatureDenominator) {
        return ChangeFlags::None;
    }

    state.tempo.timeSignatureNumerator = num;
    state.tempo.timeSignatureDenominator = den;

    // Notify audio engine of time signature change
    for (auto* listener : audioEngineListeners) {
        listener->onTimeSignatureChanged(num, den);
    }

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

TimelineController::ChangeFlags TimelineController::handleEvent(const SetGridQuantizeEvent& e) {
    auto& gq = state.display.gridQuantize;
    if (gq.autoGrid == e.autoGrid && gq.numerator == e.numerator &&
        gq.denominator == e.denominator) {
        return ChangeFlags::None;
    }

    gq.autoGrid = e.autoGrid;
    gq.numerator = e.numerator;
    gq.denominator = e.denominator;
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

    if (state.punch.isValid()) {
        state.punch.endTime = juce::jmin(state.punch.endTime, state.timelineLength);
        if (state.punch.startTime >= state.punch.endTime) {
            state.punch.clear();
        }
    }

    clampScrollPosition();

    return ChangeFlags::Timeline | ChangeFlags::Zoom | ChangeFlags::Scroll;
}

// ===== Notification Helpers =====

void TimelineController::notifyListeners(ChangeFlags changes) {
    for (auto* listener : listeners) {
        listener->timelineStateChanged(state, changes);
    }
}

// ===== Helper Methods =====

void TimelineController::clampScrollPosition() {
    int maxX = state.getMaxScrollX();
    state.zoom.scrollX = juce::jlimit(0, maxX, state.zoom.scrollX);
    state.zoom.scrollY = juce::jmax(0, state.zoom.scrollY);
}

double TimelineController::clampZoom(double zoom) const {
    auto& config = magda::Config::getInstance();
    double minZoom = state.getMinZoom();
    minZoom = juce::jmax(minZoom, config.getMinZoomLevel());
    double maxZoom = config.getMaxZoomLevel();

    return juce::jlimit(minZoom, maxZoom, zoom);
}

}  // namespace magda
