#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <set>
#include <variant>

#include "TimelineState.hpp"

namespace magda {

// ===== Zoom Events =====

/**
 * @brief Set zoom to a specific value
 */
struct SetZoomEvent {
    double zoom;
};

/**
 * @brief Set zoom centered at a specific time position
 */
struct SetZoomCenteredEvent {
    double zoom;
    double centerTime;
};

/**
 * @brief Set zoom while keeping a screen position anchored
 */
struct SetZoomAnchoredEvent {
    double zoom;
    double anchorTime;
    int anchorScreenX;
};

/**
 * @brief Zoom to fit a time range in the viewport
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
 * @brief Scroll to make a time position visible (centered if possible)
 */
struct ScrollToTimeEvent {
    double time;
    bool center = true;
};

// ===== Playhead Events =====

/**
 * @brief Set edit position (the triangle/return point)
 *
 * This is the primary way to set where playback starts from.
 * Also syncs playbackPosition to editPosition when not playing.
 */
struct SetEditPositionEvent {
    double position;
};

/**
 * @brief Set playhead position (backwards compatible alias)
 *
 * For backwards compatibility, this behaves like SetEditPositionEvent.
 */
struct SetPlayheadPositionEvent {
    double position;
};

/**
 * @brief Set playback position only (used by timer during playback)
 *
 * Only updates the playbackPosition (the moving cursor), not the editPosition.
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
 * @brief Toggle record-arm on the selected track
 *
 * Does NOT start the transport. When a track is armed and the user presses Play,
 * recording will start automatically via StartPlaybackEvent.
 */
struct StartRecordEvent {};

/**
 * @brief Move playhead by a delta amount (in seconds)
 */
struct MovePlayheadByDeltaEvent {
    double deltaSeconds;
};

/**
 * @brief Set edit cursor position (separate from playhead)
 *
 * The edit cursor is used for split/edit operations and is independent
 * from the playhead position. Set by clicking in the lower track zone.
 * Use position = -1.0 to hide/clear the edit cursor.
 */
struct SetEditCursorEvent {
    double position;
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
 * @brief Set time selection range
 *
 * trackIndices specifies which tracks are selected.
 * Empty set = all tracks (backward compatible).
 */
struct SetTimeSelectionEvent {
    double startTime;
    double endTime;
    std::set<int> trackIndices;  // Empty = all tracks
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
 * @brief Set loop region
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
 * @brief Move entire loop region by a delta
 */
struct MoveLoopRegionEvent {
    double deltaSeconds;
};

// ===== Punch In/Out Events =====

/**
 * @brief Set punch in/out region
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

// ===== Section Events =====

/**
 * @brief Add a new arrangement section
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
 * @brief Move an arrangement section
 */
struct MoveSectionEvent {
    int index;
    double newStartTime;
};

/**
 * @brief Resize an arrangement section
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
 * @brief Set timeline length
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
    SetZoomEvent, SetZoomCenteredEvent, SetZoomAnchoredEvent, ZoomToFitEvent, ResetZoomEvent,
    // Scroll events
    SetScrollPositionEvent, ScrollByDeltaEvent, ScrollToTimeEvent,
    // Playhead events
    SetEditPositionEvent, SetPlayheadPositionEvent, SetPlaybackPositionEvent, StartPlaybackEvent,
    StopPlaybackEvent, StartRecordEvent, MovePlayheadByDeltaEvent, SetPlaybackStateEvent,
    SetEditCursorEvent,
    // Selection events
    SetTimeSelectionEvent, ClearTimeSelectionEvent, CreateLoopFromSelectionEvent,
    // Loop events
    SetLoopRegionEvent, ClearLoopRegionEvent, SetLoopEnabledEvent, MoveLoopRegionEvent,
    // Punch in/out events
    SetPunchRegionEvent, ClearPunchRegionEvent, SetPunchInEnabledEvent, SetPunchOutEnabledEvent,
    // Tempo events
    SetTempoEvent, SetTimeSignatureEvent,
    // Display events
    SetTimeDisplayModeEvent, SetSnapEnabledEvent, SetArrangementLockedEvent, SetGridQuantizeEvent,
    // Section events
    AddSectionEvent, RemoveSectionEvent, MoveSectionEvent, ResizeSectionEvent, SelectSectionEvent,
    // Viewport events
    ViewportResizedEvent, SetTimelineLengthEvent>;

}  // namespace magda
