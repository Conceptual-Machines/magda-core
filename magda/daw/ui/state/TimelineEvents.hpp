#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <set>
#include <utility>
#include <variant>

#include "TimelineState.hpp"

namespace magda {

// ===== Zoom Events =====

/**
 * @brief Set horizontal timeline zoom to a specific pixels-per-beat value
 */
struct SetZoomEvent {
    double pixelsPerBeat;
};

/**
 * @brief Set horizontal timeline zoom centered at a specific beat position
 */
struct SetZoomCenteredBeatsEvent {
    double pixelsPerBeat;
    double centerBeats;
};

/**
 * @brief Set horizontal timeline zoom centered at a specific time position (compatibility boundary)
 */
struct SetZoomCenteredEvent {
    double pixelsPerBeat;
    double centerTime;
};

/**
 * @brief Set horizontal timeline zoom while keeping a beat position anchored
 */
struct SetZoomAnchoredBeatsEvent {
    double pixelsPerBeat;
    double anchorBeats;
    int anchorScreenX;
};

/**
 * @brief Set horizontal timeline zoom while keeping a screen position anchored
 * (compatibility boundary)
 */
struct SetZoomAnchoredEvent {
    double pixelsPerBeat;
    double anchorTime;
    int anchorScreenX;
};

/**
 * @brief Zoom to fit a beat range in the viewport
 */
struct ZoomToFitBeatsEvent {
    double startBeats;
    double endBeats;
    double paddingPercent = 0.05;  // 5% padding on each side
};

/**
 * @brief Zoom to fit a time range in the viewport (compatibility boundary)
 */
struct ZoomToFitEvent {
    double startTime;
    double endTime;
    double paddingPercent = 0.05;  // 5% padding on each side
};

/**
 * @brief Reset zoom to fit entire timeline
 */
struct ResetZoomEvent {};

// ===== Scroll Events =====

/**
 * @brief Set scroll position directly
 */
struct SetScrollPositionEvent {
    int scrollX;
    int scrollY = -1;  // -1 means don't change
};

/**
 * @brief Scroll by a delta amount
 */
struct ScrollByDeltaEvent {
    int deltaX;
    int deltaY;
};

/**
 * @brief Scroll to make a beat position visible (centered if possible)
 */
struct ScrollToBeatEvent {
    double beat;
    bool center = true;
};

/**
 * @brief Scroll to make a time position visible (centered if possible, compatibility boundary)
 */
struct ScrollToTimeEvent {
    double time;
    bool center = true;
};

// ===== Playhead Events =====

/**
 * @brief Set edit position in beats (the triangle/return point)
 *
 * This is the primary way to set where playback starts from.
 * Also syncs playbackPosition to editPosition when not playing.
 */
struct SetEditPositionBeatsEvent {
    double positionBeats;
};

/**
 * @brief Set edit position in seconds (compatibility boundary)
 */
struct SetEditPositionEvent {
    double position;
};

/**
 * @brief Set playhead position in beats (backwards compatible alias)
 *
 * For backwards compatibility, this behaves like SetEditPositionEvent.
 */
struct SetPlayheadPositionBeatsEvent {
    double positionBeats;
};

/**
 * @brief Set playhead position in seconds (compatibility boundary)
 */
struct SetPlayheadPositionEvent {
    double position;
};

/**
 * @brief Set playback position only in beats
 *
 * Only updates the playbackPosition (the moving cursor), not the editPosition.
 */
struct SetPlaybackPositionBeatsEvent {
    double positionBeats;
};

/**
 * @brief Set playback position only in seconds (audio-engine compatibility boundary)
 */
struct SetPlaybackPositionEvent {
    double position;
};

/**
 * @brief Start playback (syncs playbackPosition to editPosition)
 */
struct StartPlaybackEvent {};

/**
 * @brief Stop playback (resets playbackPosition to editPosition)
 */
struct StopPlaybackEvent {};

/**
 * @brief Start recording on armed tracks
 *
 * If no tracks are armed, this is a no-op.
 * If not already playing, starts both playback and recording.
 * If already playing, punch-in records from the current position.
 * If already recording, punch-out (stops recording, keeps playing).
 */
struct StartRecordEvent {};

/**
 * @brief Move playhead by a delta amount (in beats)
 */
struct MovePlayheadByDeltaBeatsEvent {
    double deltaBeats;
};

/**
 * @brief Move playhead by a delta amount (in seconds, compatibility boundary)
 */
struct MovePlayheadByDeltaEvent {
    double deltaSeconds;
};

/**
 * @brief Set edit cursor position (separate from playhead)
 *
 * The edit cursor is used for split/edit operations and is independent
 * from the playhead position. Set by clicking in the lower track zone.
 * Use positionBeats = -1.0 to hide/clear the edit cursor.
 */
struct SetEditCursorEvent {
    double positionBeats;
};

/**
 * @brief Set playback state
 */
struct SetPlaybackStateEvent {
    bool isPlaying;
    bool isRecording = false;
};

// ===== Selection Events =====

/**
 * @brief Set time selection range in beats
 *
 * trackIndices specifies which tracks are selected.
 * Empty set = all tracks (backward compatible).
 */
struct SetTimeSelectionBeatsEvent {
    SetTimeSelectionBeatsEvent(double startBeatsIn, double endBeatsIn, std::set<int> trackIndicesIn,
                               bool automationOnlyIn = false,
                               std::set<AutomationLaneId> automationLaneIdsIn = {})
        : startBeats(startBeatsIn),
          endBeats(endBeatsIn),
          trackIndices(std::move(trackIndicesIn)),
          automationOnly(automationOnlyIn),
          automationLaneIds(std::move(automationLaneIdsIn)) {}

    double startBeats;
    double endBeats;
    std::set<int> trackIndices;  // Empty = all tracks
    bool automationOnly = false;
    std::set<AutomationLaneId> automationLaneIds;
};

/**
 * @brief Set time selection range in seconds (compatibility boundary)
 */
struct SetTimeSelectionEvent {
    SetTimeSelectionEvent(double startTimeIn, double endTimeIn, std::set<int> trackIndicesIn,
                          bool automationOnlyIn = false,
                          std::set<AutomationLaneId> automationLaneIdsIn = {})
        : startTime(startTimeIn),
          endTime(endTimeIn),
          trackIndices(std::move(trackIndicesIn)),
          automationOnly(automationOnlyIn),
          automationLaneIds(std::move(automationLaneIdsIn)) {}

    double startTime;
    double endTime;
    std::set<int> trackIndices;
    bool automationOnly = false;
    std::set<AutomationLaneId> automationLaneIds;
};

/**
 * @brief Clear time selection
 */
struct ClearTimeSelectionEvent {};

/**
 * @brief Create loop region from current selection
 */
struct CreateLoopFromSelectionEvent {};

// ===== Loop Events =====

/**
 * @brief Set loop region in beats
 */
struct SetLoopRegionBeatsEvent {
    double startBeats;
    double endBeats;
};

/**
 * @brief Set loop region in seconds (compatibility boundary)
 */
struct SetLoopRegionEvent {
    double startTime;
    double endTime;
};

/**
 * @brief Clear loop region
 */
struct ClearLoopRegionEvent {};

/**
 * @brief Enable or disable loop
 */
struct SetLoopEnabledEvent {
    bool enabled;
};

/**
 * @brief Move entire loop region by a beat delta
 */
struct MoveLoopRegionBeatsEvent {
    double deltaBeats;
};

/**
 * @brief Move entire loop region by a seconds delta (compatibility boundary)
 */
struct MoveLoopRegionEvent {
    double deltaSeconds;
};

// ===== Punch In/Out Events =====

/**
 * @brief Set punch in/out region in beats
 */
struct SetPunchRegionBeatsEvent {
    double startBeats;
    double endBeats;
};

/**
 * @brief Set punch in/out region in seconds (compatibility boundary)
 */
struct SetPunchRegionEvent {
    double startTime;
    double endTime;
};

/**
 * @brief Clear punch region
 */
struct ClearPunchRegionEvent {};

/**
 * @brief Enable or disable punch in
 */
struct SetPunchInEnabledEvent {
    bool enabled;
};

/**
 * @brief Enable or disable punch out
 */
struct SetPunchOutEnabledEvent {
    bool enabled;
};

// ===== Tempo Events =====

/**
 * @brief Set tempo (BPM)
 */
struct SetTempoEvent {
    double bpm;
};

/**
 * @brief Set time signature
 */
struct SetTimeSignatureEvent {
    int numerator;
    int denominator;
};

// ===== Display Events =====

/**
 * @brief Set time display mode
 */
struct SetTimeDisplayModeEvent {
    TimeDisplayMode mode;
};

/**
 * @brief Toggle snap to grid
 */
struct SetSnapEnabledEvent {
    bool enabled;
};

/**
 * @brief Set arrangement locked state
 */
struct SetArrangementLockedEvent {
    bool locked;
};

/**
 * @brief Set grid quantize (auto toggle + numerator/denominator)
 */
struct SetGridQuantizeEvent {
    bool autoGrid;
    int numerator;
    int denominator;
};

/**
 * @brief Update auto-grid effective display values (from MIDI editor zoom)
 *
 * Only updates the display numerator/denominator shown in the BottomPanel
 * when auto-grid is active. Does not affect the arrangement grid.
 */
struct SetAutoGridDisplayEvent {
    int effectiveNumerator;
    int effectiveDenominator;
};

// ===== Section Events =====

/**
 * @brief Add a new arrangement section in beats
 */
struct AddSectionBeatsEvent {
    juce::String name;
    double startBeats;
    double endBeats;
    juce::Colour colour = juce::Colours::blue;
};

/**
 * @brief Add a new arrangement section in seconds (compatibility boundary)
 */
struct AddSectionEvent {
    juce::String name;
    double startTime;
    double endTime;
    juce::Colour colour = juce::Colours::blue;
};

/**
 * @brief Remove an arrangement section
 */
struct RemoveSectionEvent {
    int index;
};

/**
 * @brief Move an arrangement section in beats
 */
struct MoveSectionBeatsEvent {
    int index;
    double newStartBeats;
};

/**
 * @brief Move an arrangement section in seconds (compatibility boundary)
 */
struct MoveSectionEvent {
    int index;
    double newStartTime;
};

/**
 * @brief Resize an arrangement section in beats
 */
struct ResizeSectionBeatsEvent {
    int index;
    double newStartBeats;
    double newEndBeats;
};

/**
 * @brief Resize an arrangement section in seconds (compatibility boundary)
 */
struct ResizeSectionEvent {
    int index;
    double newStartTime;
    double newEndTime;
};

/**
 * @brief Select an arrangement section
 */
struct SelectSectionEvent {
    int index;  // -1 to deselect
};

// ===== Viewport Events =====

/**
 * @brief Notify that viewport has been resized
 */
struct ViewportResizedEvent {
    int width;
    int height;
};

/**
 * @brief Set timeline length in beats
 */
struct SetTimelineLengthBeatsEvent {
    double lengthBeats;
};

/**
 * @brief Set timeline length in seconds (compatibility boundary)
 */
struct SetTimelineLengthEvent {
    double lengthInSeconds;
};

// ===== The unified TimelineEvent variant =====

/**
 * @brief Union of all timeline events
 *
 * Components dispatch these events to the TimelineController,
 * which processes them and updates the TimelineState accordingly.
 */
using TimelineEvent = std::variant<
    // Zoom events
    SetZoomEvent, SetZoomCenteredBeatsEvent, SetZoomCenteredEvent, SetZoomAnchoredBeatsEvent,
    SetZoomAnchoredEvent, ZoomToFitBeatsEvent, ZoomToFitEvent, ResetZoomEvent,
    // Scroll events
    SetScrollPositionEvent, ScrollByDeltaEvent, ScrollToBeatEvent, ScrollToTimeEvent,
    // Playhead events
    SetEditPositionBeatsEvent, SetEditPositionEvent, SetPlayheadPositionBeatsEvent,
    SetPlayheadPositionEvent, SetPlaybackPositionBeatsEvent, SetPlaybackPositionEvent,
    StartPlaybackEvent, StopPlaybackEvent, StartRecordEvent, MovePlayheadByDeltaBeatsEvent,
    MovePlayheadByDeltaEvent, SetPlaybackStateEvent, SetEditCursorEvent,
    // Selection events
    SetTimeSelectionBeatsEvent, SetTimeSelectionEvent, ClearTimeSelectionEvent,
    CreateLoopFromSelectionEvent,
    // Loop events
    SetLoopRegionBeatsEvent, SetLoopRegionEvent, ClearLoopRegionEvent, SetLoopEnabledEvent,
    MoveLoopRegionBeatsEvent, MoveLoopRegionEvent,
    // Punch in/out events
    SetPunchRegionBeatsEvent, SetPunchRegionEvent, ClearPunchRegionEvent, SetPunchInEnabledEvent,
    SetPunchOutEnabledEvent,
    // Tempo events
    SetTempoEvent, SetTimeSignatureEvent,
    // Display events
    SetTimeDisplayModeEvent, SetSnapEnabledEvent, SetArrangementLockedEvent, SetGridQuantizeEvent,
    SetAutoGridDisplayEvent,
    // Section events
    AddSectionBeatsEvent, AddSectionEvent, RemoveSectionEvent, MoveSectionBeatsEvent,
    MoveSectionEvent, ResizeSectionBeatsEvent, ResizeSectionEvent, SelectSectionEvent,
    // Viewport events
    ViewportResizedEvent, SetTimelineLengthBeatsEvent, SetTimelineLengthEvent>;

}  // namespace magda
