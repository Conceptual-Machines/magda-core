#include "TimelineController.hpp"

#include <algorithm>

#include "../../core/ClipManager.hpp"
#include "../../core/TempoUtils.hpp"
#include "../../core/TrackManager.hpp"
#include "../../project/ProjectManager.hpp"
#include "../utils/TimelineUtils.hpp"
#include "Config.hpp"

namespace magda {

TimelineController::TimelineController() {
    // Set as current instance for global access
    currentInstance_ = this;

    // Load configuration values (bars → seconds using default 120 BPM)
    auto& config = magda::Config::getInstance();
    state.timelineLength = state.tempo.barsToTime(config.getDefaultTimelineLengthBars());

    // Set default zoom (ppb) to show a reasonable view duration
    double defaultViewDuration = state.tempo.barsToTime(config.getDefaultZoomViewBars());
    if (defaultViewDuration > 0 && state.zoom.viewportWidth > 0) {
        double beats = state.secondsToBeats(defaultViewDuration);
        if (beats > 0)
            state.zoom.horizontalZoom = state.zoom.viewportWidth / beats;
    }

    DBG("TimelineController: initialized with timelineLength=" << state.timelineLength);
}

TimelineController::~TimelineController() {
    // Clear the current instance
    if (currentInstance_ == this) {
        currentInstance_ = nullptr;
    }
}

void TimelineController::dispatch(const TimelineEvent& event) {
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
    int timeContentX =
        static_cast<int>(centerBeats * newZoom) + LayoutConfig::TIMELINE_LEFT_PADDING;
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
    int anchorPixelPos =
        static_cast<int>(anchorBeats * newZoom) + LayoutConfig::TIMELINE_LEFT_PADDING;
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
    int availableWidth = state.zoom.viewportWidth - LayoutConfig::TIMELINE_LEFT_PADDING;
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
    int targetX =
        static_cast<int>(beats * state.zoom.horizontalZoom) + LayoutConfig::TIMELINE_LEFT_PADDING;

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

    // === Punch In: trigger recording when playhead reaches punch-in point ===
    if (punchArmed_ && newPos >= state.punch.startTime) {
        punchArmed_ = false;
        DBG("SetPlaybackPositionEvent: punch-in triggered at " << state.punch.startTime);
        for (auto* listener : audioEngineListeners) {
            listener->onTransportRecord(state.punch.startTime);
        }
    }

    // === Punch Out: stop recording when playhead reaches punch-out point ===
    if (state.playhead.isRecording && !punchArmed_ && state.punch.punchOutEnabled &&
        state.punch.isValid() && newPos >= state.punch.endTime) {
        DBG("SetPlaybackPositionEvent: punch-out triggered at " << state.punch.endTime);
        state.playhead.isRecording = false;
        for (auto* listener : audioEngineListeners) {
            listener->onTransportStopRecording();
        }
    }

    return ChangeFlags::Playhead;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const StartPlaybackEvent& /*e*/) {
    if (state.playhead.isPlaying) {
        return ChangeFlags::None;  // Already playing
    }

    state.playhead.isPlaying = true;
    // Sync playbackPosition to editPosition at start of playback
    state.playhead.playbackPosition = state.playhead.editPosition;

    for (auto* listener : audioEngineListeners) {
        listener->onTransportPlay(state.playhead.editPosition);
    }

    return ChangeFlags::Playhead;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const StartRecordEvent& /*e*/) {
    // If punch-armed but not yet actually recording, cancel the armed state
    // (isRecording is true for UI purposes but TE hasn't started recording yet)
    if (punchArmed_) {
        DBG("StartRecordEvent: cancelling punch-armed state");
        punchArmed_ = false;
        state.playhead.isRecording = false;
        return ChangeFlags::Playhead;
    }

    // If currently recording, punch out (stop recording, keep playing)
    if (state.playhead.isRecording) {
        DBG("StartRecordEvent: punch out (stop recording, keep playing)");
        state.playhead.isRecording = false;
        for (auto* listener : audioEngineListeners) {
            listener->onTransportStopRecording();
        }
        return ChangeFlags::Playhead;
    }

    // Session recording does not require armed tracks — proceed regardless.
    // (Track arming is only needed for MIDI/audio input recording via TE.)

    // Check if punch-in is enabled with a valid region
    bool punchInActive = state.punch.punchInEnabled && state.punch.isValid();

    if (punchInActive) {
        // Determine the position we'll be playing from
        double startPos = state.playhead.isPlaying ? state.playhead.playbackPosition
                                                   : state.playhead.editPosition;

        if (startPos < state.punch.startTime) {
            // Playhead is before punch-in point — arm and start playback, defer recording
            DBG("StartRecordEvent: punch-in armed, waiting for position "
                << state.punch.startTime << " (current: " << startPos << ")");
            punchArmed_ = true;
            state.playhead.isRecording = true;  // UI shows recording state

            if (!state.playhead.isPlaying) {
                state.playhead.isPlaying = true;
                state.playhead.playbackPosition = state.playhead.editPosition;
                for (auto* listener : audioEngineListeners) {
                    listener->onTransportPlay(state.playhead.editPosition);
                }
            }
            return ChangeFlags::Playhead;
        }
        // Playhead is already past punch-in point — start recording immediately (fall through)
    }

    if (state.playhead.isPlaying) {
        // Punch-in: already playing, start recording now
        DBG("StartRecordEvent: punch-in recording at " << state.playhead.playbackPosition);
        state.playhead.isRecording = true;
        for (auto* listener : audioEngineListeners) {
            listener->onTransportRecord(state.playhead.playbackPosition);
        }
    } else {
        // Start recording from edit position (also starts playback)
        DBG("StartRecordEvent: starting recording at " << state.playhead.editPosition);
        state.playhead.isPlaying = true;
        state.playhead.isRecording = true;
        state.playhead.playbackPosition = state.playhead.editPosition;
        for (auto* listener : audioEngineListeners) {
            listener->onTransportRecord(state.playhead.editPosition);
        }
    }

    return ChangeFlags::Playhead;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const StopPlaybackEvent& /*e*/) {
    DBG("[TimelineController] handleEvent(StopPlaybackEvent) isPlaying="
        << (int)state.playhead.isPlaying);
    if (!state.playhead.isPlaying) {
        return ChangeFlags::None;  // Already stopped
    }

    // Capture where playback was at the moment of stopping, before any
    // resets. The "stop updates playhead" preference uses this to move
    // editPosition to the stop point so the next Play resumes from there.
    const double stopPosition = state.playhead.playbackPosition;

    state.playhead.isPlaying = false;
    state.playhead.isRecording = false;
    punchArmed_ = false;

    if (magda::Config::getInstance().getStopUpdatesPlayhead()) {
        state.playhead.editPosition = stopPosition;
        state.playhead.editPositionBeats =
            magda::TimelineUtils::secondsToBeats(stopPosition, state.tempo.bpm);
        state.playhead.playbackPosition = stopPosition;
    } else {
        // Default Bitwig behavior: rewind playbackPosition to editPosition
        // so the next Play restarts from where the playhead was before.
        state.playhead.playbackPosition = state.playhead.editPosition;
    }

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
    double newBeats = e.positionBeats;

    // Allow -1.0 to hide the cursor, otherwise clamp to the timeline in beats.
    if (newBeats >= 0.0) {
        newBeats = juce::jlimit(0.0, state.secondsToBeats(state.timelineLength), newBeats);
    }

    if (newBeats == state.editCursorBeats) {
        return ChangeFlags::None;
    }

    state.editCursorBeats = newBeats;
    state.editCursorPosition = newBeats >= 0.0 ? state.beatsToSeconds(newBeats) : -1.0;
    // Use Selection flag since edit cursor is an editing-related visual
    return ChangeFlags::Selection;
}

// ===== Selection Event Handlers =====

TimelineController::ChangeFlags TimelineController::handleEvent(const SetTimeSelectionEvent& e) {
    double start = juce::jlimit(0.0, state.timelineLength, e.startTime);
    double end = juce::jlimit(0.0, state.timelineLength, e.endTime);

    state.selection.setFromSeconds(start, end, state.tempo.bpm);
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

    state.loop.setFromBeats(state.selection.startBeats, state.selection.endBeats, state.tempo.bpm);
    state.loop.enabled = true;

    ProjectManager::getInstance().setLoopSettings(state.loop.enabled, state.loop.startBeats,
                                                  state.loop.endBeats);

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

    // Ensure minimum duration
    if (end - start < 0.01) {
        end = start + 0.01;
    }

    state.loop.setFromSeconds(start, end, state.tempo.bpm);

    // Enable loop if it wasn't valid before
    if (!state.loop.enabled && state.loop.isValid()) {
        state.loop.enabled = true;
    }

    ProjectManager::getInstance().setLoopSettings(state.loop.enabled, state.loop.startBeats,
                                                  state.loop.endBeats);

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
    ProjectManager::getInstance().setLoopSettings(false, 0.0, 0.0);
    return ChangeFlags::Loop;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const SetLoopEnabledEvent& e) {
    if (state.loop.isValid()) {
        // Region exists — pure on/off toggle. The region stays exactly where the user
        // placed it; we never relocate on enable/disable.
        if (state.loop.enabled == e.enabled) {
            return ChangeFlags::None;
        }

        state.loop.enabled = e.enabled;
        ProjectManager::getInstance().setLoopSettings(e.enabled, state.loop.startBeats,
                                                      state.loop.endBeats);

        for (auto* listener : audioEngineListeners) {
            listener->onLoopEnabledChanged(e.enabled);
        }
        return ChangeFlags::Loop;
    }

    // No valid region. Disabling is a no-op; enabling seeds a 1-bar default at the playhead
    // so the loop button is always actionable from a fresh project.
    if (!e.enabled) {
        return ChangeFlags::None;
    }

    constexpr double minLoopDuration = 0.01;
    if (state.timelineLength < minLoopDuration) {
        return ChangeFlags::None;
    }

    const double defaultDuration = juce::jmax(minLoopDuration, state.tempo.getSecondsPerBar());
    double start = juce::jlimit(0.0, state.timelineLength, state.playhead.getCurrentPosition());
    double end = juce::jmin(state.timelineLength, start + defaultDuration);
    if (end - start < minLoopDuration) {
        end = juce::jlimit(minLoopDuration, state.timelineLength, end);
        start = juce::jmax(0.0, end - minLoopDuration);
    }

    state.loop.setFromSeconds(start, end, state.tempo.bpm);
    state.loop.enabled = true;

    ProjectManager::getInstance().setLoopSettings(true, state.loop.startBeats, state.loop.endBeats);

    for (auto* listener : audioEngineListeners) {
        listener->onLoopRegionChanged(start, end, true);
    }

    return ChangeFlags::Loop;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const MoveLoopRegionEvent& e) {
    if (!state.loop.isValid()) {
        return ChangeFlags::None;
    }

    double durationBeats = state.loop.getDurationBeats();
    double newStartBeats = state.loop.startBeats + state.secondsToBeats(e.deltaSeconds);
    double timelineLengthBeats = state.secondsToBeats(state.timelineLength);

    // Clamp to valid range
    newStartBeats = juce::jlimit(0.0, timelineLengthBeats - durationBeats, newStartBeats);
    state.loop.setFromBeats(newStartBeats, newStartBeats + durationBeats, state.tempo.bpm);

    ProjectManager::getInstance().setLoopSettings(state.loop.enabled, state.loop.startBeats,
                                                  state.loop.endBeats);

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

    state.punch.setFromSeconds(start, end, state.tempo.bpm);

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

    punchArmed_ = false;
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

    // If punch-in is disabled while armed, cancel the armed state
    if (!e.enabled && punchArmed_) {
        punchArmed_ = false;
    }

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
    double newBpm = clampBpm(e.bpm);
    if (newBpm == state.tempo.bpm) {
        return ChangeFlags::None;
    }

    double oldBpm = state.tempo.bpm;
    state.tempo.bpm = newBpm;

    // Keep ProjectManager in sync for serialization
    ProjectManager::getInstance().setTempo(newBpm);

    // Update all beat-anchored positions to maintain bar/beat positions
    uint32_t extraFlags = 0;

    // --- Edit cursor (split/paste cursor) ---
    if (state.editCursorPosition >= 0.0) {
        if (state.editCursorBeats < 0.0) {
            state.editCursorBeats =
                magda::TimelineUtils::secondsToBeats(state.editCursorPosition, oldBpm);
        }
        state.editCursorPosition =
            magda::TimelineUtils::beatsToSeconds(state.editCursorBeats, newBpm);
        extraFlags |= static_cast<uint32_t>(ChangeFlags::Selection);
    }

    // --- Playhead edit position ---
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
        state.selection.setFromBeats(state.selection.startBeats, state.selection.endBeats, newBpm);
        extraFlags |= static_cast<uint32_t>(ChangeFlags::Selection);
    }

    // --- Punch region ---
    if (state.punch.isValid()) {
        state.punch.setFromBeats(state.punch.startBeats, state.punch.endBeats, newBpm);
        extraFlags |= static_cast<uint32_t>(ChangeFlags::Punch);
    }

    // --- Loop region ---
    if (state.loop.isValid()) {
        state.loop.setFromBeats(state.loop.startBeats, state.loop.endBeats, newBpm);
        extraFlags |= static_cast<uint32_t>(ChangeFlags::Loop);
    }

    // Sync updated loop to ProjectManager
    if (state.loop.isValid()) {
        ProjectManager::getInstance().setLoopSettings(state.loop.enabled, state.loop.startBeats,
                                                      state.loop.endBeats);
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

    // Update beat-authoritative clips when project tempo changes.
    //
    // Beats are the canonical state for these clips — seconds are a derived
    // cache. ClipManager::refreshDerivedSeconds is the single source of truth
    // for that derivation; calling it here keeps every reader (renderers,
    // ClipSynchronizer, inspector readouts) consistent without duplicating
    // formulas. Process clips of every view (arrangement AND session) — the
    // earlier session-only skip caused issue #1157: session autoTempo clips
    // kept stale `length` / `startTime` after a project-tempo change, which
    // made the inspector beat readout disagree with the rendered loop region.
    if (std::abs(newBpm - oldBpm) > 0.01) {
        auto& clipManager = ClipManager::getInstance();
        auto allClips = clipManager.getClips();

        std::vector<ClipId> updatedClipIds;
        for (const auto& clip : allClips) {
            auto* mutableClip = clipManager.getClip(clip.id);
            if (!mutableClip)
                continue;

            // Legacy migration: old projects may have meaningful startTime/length
            // caches while placement is still at its default value. Once a clip
            // has explicit beat placement, never derive beats back from the
            // seconds cache on tempo changes; the cache may be stale.
            constexpr double eps = 0.000001;
            double startBeats = mutableClip->placement.startBeat;
            double lengthBeats = mutableClip->placement.lengthBeats;

            const bool hasBeatStart = startBeats > eps || mutableClip->startBeats > eps;
            const bool hasBeatLength = lengthBeats > eps || mutableClip->lengthBeats > eps;

            if (startBeats <= eps && mutableClip->startBeats > eps)
                startBeats = mutableClip->startBeats;
            if (!hasBeatStart && mutableClip->startTime > eps)
                startBeats = magda::TimelineUtils::secondsToBeats(mutableClip->startTime, oldBpm);

            if (lengthBeats <= eps && mutableClip->lengthBeats > eps)
                lengthBeats = mutableClip->lengthBeats;

            if (!hasBeatLength && mutableClip->length > eps)
                lengthBeats = magda::TimelineUtils::secondsToBeats(mutableClip->length, oldBpm);

            mutableClip->setPlacementBeats(startBeats, lengthBeats);

            // Beat-authoritative path: refresh the seconds cache from beats.
            clipManager.refreshDerivedSeconds(clip.id, newBpm);
            updatedClipIds.push_back(clip.id);
        }

        // Notify so AudioBridge re-syncs TE positions and the UI re-reads.
        for (auto clipId : updatedClipIds) {
            clipManager.forceNotifyClipPropertyChanged(clipId);
        }
    }

    // Return combined flags for all updated state
    return static_cast<ChangeFlags>(static_cast<uint32_t>(ChangeFlags::Tempo) | extraFlags);
}

TimelineController::ChangeFlags TimelineController::handleEvent(const SetTimeSignatureEvent& e) {
    int num = clampTimeSignatureValue(e.numerator);
    int den = clampTimeSignatureValue(e.denominator);

    if (num == state.tempo.timeSignatureNumerator && den == state.tempo.timeSignatureDenominator) {
        return ChangeFlags::None;
    }

    state.tempo.timeSignatureNumerator = num;
    state.tempo.timeSignatureDenominator = den;

    ProjectManager::getInstance().setTimeSignature(num, den);

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

    int newNum = e.numerator;
    int newDen = e.denominator;

    // When switching from auto to manual, seed from the last auto-computed value
    if (gq.autoGrid && !e.autoGrid) {
        if (gq.autoEffectiveDenominator > 0) {
            // Note fraction (e.g. 1/8, 1/16)
            newNum = gq.autoEffectiveNumerator;
            newDen = gq.autoEffectiveDenominator;
        } else {
            // Bar multiple — convert to note fraction using time signature
            // e.g. 1 bar in 4/4 → 4/1, 2 bars in 4/4 → 8/1
            newNum = gq.autoEffectiveNumerator * state.tempo.timeSignatureNumerator;
            newDen = 1;
        }
    }

    if (gq.autoGrid == e.autoGrid && gq.numerator == newNum && gq.denominator == newDen) {
        return ChangeFlags::None;
    }

    gq.autoGrid = e.autoGrid;
    gq.numerator = newNum;
    gq.denominator = newDen;
    return ChangeFlags::Display;
}

TimelineController::ChangeFlags TimelineController::handleEvent(const SetAutoGridDisplayEvent& e) {
    auto& gq = state.display.gridQuantize;
    if (gq.autoEffectiveNumerator == e.effectiveNumerator &&
        gq.autoEffectiveDenominator == e.effectiveDenominator) {
        return ChangeFlags::None;
    }
    gq.autoEffectiveNumerator = e.effectiveNumerator;
    gq.autoEffectiveDenominator = e.effectiveDenominator;
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
    if (state.editCursorBeats >= 0.0) {
        state.editCursorBeats =
            juce::jmin(state.editCursorBeats, state.secondsToBeats(state.timelineLength));
        state.editCursorPosition = state.beatsToSeconds(state.editCursorBeats);
    }

    if (state.loop.isValid()) {
        const double timelineLengthBeats = state.secondsToBeats(state.timelineLength);
        const double endBeats = juce::jmin(state.loop.endBeats, timelineLengthBeats);
        if (state.loop.startBeats >= endBeats) {
            state.loop.clear();
        } else {
            state.loop.setFromBeats(state.loop.startBeats, endBeats, state.tempo.bpm);
        }
    }

    if (state.punch.isValid()) {
        const double timelineLengthBeats = state.secondsToBeats(state.timelineLength);
        const double endBeats = juce::jmin(state.punch.endBeats, timelineLengthBeats);
        if (state.punch.startBeats >= endBeats) {
            state.punch.clear();
        } else {
            state.punch.setFromBeats(state.punch.startBeats, endBeats, state.tempo.bpm);
        }
    }

    clampScrollPosition();

    return ChangeFlags::Timeline | ChangeFlags::Zoom | ChangeFlags::Scroll;
}

// ===== Project Restore =====

void TimelineController::restoreProjectState(double tempo, int timeSigNum, int timeSigDen,
                                             bool loopEnabled, double loopStartBeats,
                                             double loopEndBeats) {
    // Unconditionally set state — no early returns
    state.tempo.bpm = clampBpm(tempo);
    state.tempo.timeSignatureNumerator = clampTimeSignatureValue(timeSigNum);
    state.tempo.timeSignatureDenominator = clampTimeSignatureValue(timeSigDen);

    // Recalculate timeline length from configured bars using actual project tempo
    auto& config = magda::Config::getInstance();
    state.timelineLength = state.tempo.barsToTime(config.getDefaultTimelineLengthBars());

    // Loop: beats are authoritative, derive seconds from BPM
    state.loop.enabled = loopEnabled;

    if (loopEndBeats - loopStartBeats >= 0.01) {
        state.loop.setFromBeats(loopStartBeats, loopEndBeats, state.tempo.bpm);
        state.loop.enabled = loopEnabled;
    } else {
        state.loop.clear();
    }

    // Notify audio engine unconditionally
    for (auto* listener : audioEngineListeners) {
        listener->onTempoChanged(state.tempo.bpm);
        listener->onTimeSignatureChanged(state.tempo.timeSignatureNumerator,
                                         state.tempo.timeSignatureDenominator);
        if (state.loop.isValid()) {
            listener->onLoopRegionChanged(state.loop.startTime, state.loop.endTime,
                                          state.loop.enabled);
        }
    }

    // Notify UI listeners
    notifyListeners(ChangeFlags::Tempo | ChangeFlags::Loop);
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
