#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <deque>
#include <memory>
#include <vector>

#include "TimelineEvents.hpp"
#include "TimelineState.hpp"
#include "TransportStateListener.hpp"

namespace magda {

/**
 * @brief Listener interface for timeline state changes
 *
 * Components implement this interface to receive notifications
 * when the timeline state changes.
 */
class TimelineStateListener {
  public:
    virtual ~TimelineStateListener() = default;

    /**
     * Called when any part of the timeline state changes.
     * This is always called after more specific notifications.
     */
    virtual void timelineStateChanged(const TimelineState& state) = 0;

    /**
     * Called specifically when zoom or scroll state changes.
     * Override for optimized handling of frequent updates.
     */
    virtual void zoomStateChanged(const TimelineState& state) {
        // Default: fall through to general handler
        timelineStateChanged(state);
    }

    /**
     * Called specifically when playhead position changes.
     * Override for optimized handling of frequent updates.
     */
    virtual void playheadStateChanged(const TimelineState& state) {
        // Default: fall through to general handler
        timelineStateChanged(state);
    }

    /**
     * Called specifically when time selection changes.
     */
    virtual void selectionStateChanged(const TimelineState& state) {
        // Default: fall through to general handler
        timelineStateChanged(state);
    }

    /**
     * Called specifically when loop region changes.
     */
    virtual void loopStateChanged(const TimelineState& state) {
        // Default: fall through to general handler
        timelineStateChanged(state);
    }

    /**
     * Called specifically when tempo or time signature changes.
     */
    virtual void tempoStateChanged(const TimelineState& state) {
        // Default: fall through to general handler
        timelineStateChanged(state);
    }

    /**
     * Called specifically when display settings change.
     */
    virtual void displayConfigChanged(const TimelineState& state) {
        // Default: fall through to general handler
        timelineStateChanged(state);
    }
};

/**
 * @brief Central controller for timeline state management
 *
 * The TimelineController owns the single source of truth (TimelineState)
 * and provides:
 * - Event dispatching for state modifications
 * - Listener notification for state changes
 * - Undo/redo support
 *
 * Data flow:
 *   User Input -> Component -> dispatch(Event) -> TimelineController
 *   -> Update State -> Notify Listeners -> Repaint
 */
class TimelineController {
  public:
    TimelineController();
    ~TimelineController();

    // ===== Global Access =====

    /**
     * Get the current TimelineController instance.
     * Returns nullptr if not yet initialized.
     */
    static TimelineController* getCurrent() {
        return currentInstance_;
    }

    // ===== State Access =====

    /**
     * Get read-only access to the current state.
     * This is the ONLY way components should read timeline state.
     */
    const TimelineState& getState() const {
        return state;
    }

    // ===== Event Dispatching =====

    /**
     * Dispatch an event to modify the timeline state.
     * This is the ONLY way to modify timeline state.
     */
    void dispatch(const TimelineEvent& event);

    // ===== Listener Management =====

    /**
     * Add a listener to receive state change notifications.
     */
    void addListener(TimelineStateListener* listener);

    /**
     * Remove a previously added listener.
     */
    void removeListener(TimelineStateListener* listener);

    // ===== Audio Engine Listener Management =====

    /**
     * Add an audio engine listener to receive state changes.
     * The audio engine implements this to respond to transport, tempo, loop changes.
     */
    void addAudioEngineListener(AudioEngineListener* listener);

    /**
     * Remove a previously added audio engine listener.
     */
    void removeAudioEngineListener(AudioEngineListener* listener);

    // ===== Undo/Redo =====

    /**
     * Push the current state onto the undo stack.
     * Call this before making significant changes that should be undoable.
     */
    void pushUndoState();

    /**
     * Undo the last state change.
     * @return true if undo was performed
     */
    bool undo();

    /**
     * Redo a previously undone state change.
     * @return true if redo was performed
     */
    bool redo();

    /**
     * Check if undo is available.
     */
    bool canUndo() const {
        return !undoStack.empty();
    }

    /**
     * Check if redo is available.
     */
    bool canRedo() const {
        return !redoStack.empty();
    }

    /**
     * Clear the undo/redo history.
     */
    void clearUndoHistory();

    // ===== Configuration =====

    /**
     * Set the maximum number of undo states to keep.
     */
    void setMaxUndoStates(size_t maxStates) {
        maxUndoStates = maxStates;
    }

    // ===== Change Flags =====
    // Indicates what parts of state changed (public for helper functions)
    enum class ChangeFlags : uint32_t {
        None = 0,
        Zoom = 1 << 0,
        Scroll = 1 << 1,
        Playhead = 1 << 2,
        Selection = 1 << 3,
        Loop = 1 << 4,
        Tempo = 1 << 5,
        Display = 1 << 6,
        Sections = 1 << 7,
        Timeline = 1 << 8,
        All = 0xFFFFFFFF
    };

  private:
    // The single source of truth
    TimelineState state;

    // Listeners
    std::vector<TimelineStateListener*> listeners;
    std::vector<AudioEngineListener*> audioEngineListeners;

    // Undo/redo stacks
    std::deque<TimelineState> undoStack;
    std::deque<TimelineState> redoStack;
    size_t maxUndoStates = 50;

    // ===== Event Handlers =====
    // Each handler modifies state and returns flags indicating what changed

    ChangeFlags handleEvent(const SetZoomEvent& e);
    ChangeFlags handleEvent(const SetZoomCenteredEvent& e);
    ChangeFlags handleEvent(const SetZoomAnchoredEvent& e);
    ChangeFlags handleEvent(const ZoomToFitEvent& e);
    ChangeFlags handleEvent(const ResetZoomEvent& e);

    ChangeFlags handleEvent(const SetScrollPositionEvent& e);
    ChangeFlags handleEvent(const ScrollByDeltaEvent& e);
    ChangeFlags handleEvent(const ScrollToTimeEvent& e);

    ChangeFlags handleEvent(const SetEditPositionEvent& e);
    ChangeFlags handleEvent(const SetPlayheadPositionEvent& e);
    ChangeFlags handleEvent(const SetPlaybackPositionEvent& e);
    ChangeFlags handleEvent(const StartPlaybackEvent& e);
    ChangeFlags handleEvent(const StopPlaybackEvent& e);
    ChangeFlags handleEvent(const MovePlayheadByDeltaEvent& e);
    ChangeFlags handleEvent(const SetPlaybackStateEvent& e);
    ChangeFlags handleEvent(const SetEditCursorEvent& e);

    ChangeFlags handleEvent(const SetTimeSelectionEvent& e);
    ChangeFlags handleEvent(const ClearTimeSelectionEvent& e);
    ChangeFlags handleEvent(const CreateLoopFromSelectionEvent& e);

    ChangeFlags handleEvent(const SetLoopRegionEvent& e);
    ChangeFlags handleEvent(const ClearLoopRegionEvent& e);
    ChangeFlags handleEvent(const SetLoopEnabledEvent& e);
    ChangeFlags handleEvent(const MoveLoopRegionEvent& e);

    ChangeFlags handleEvent(const SetTempoEvent& e);
    ChangeFlags handleEvent(const SetTimeSignatureEvent& e);

    ChangeFlags handleEvent(const SetTimeDisplayModeEvent& e);
    ChangeFlags handleEvent(const SetSnapEnabledEvent& e);
    ChangeFlags handleEvent(const SetArrangementLockedEvent& e);

    ChangeFlags handleEvent(const AddSectionEvent& e);
    ChangeFlags handleEvent(const RemoveSectionEvent& e);
    ChangeFlags handleEvent(const MoveSectionEvent& e);
    ChangeFlags handleEvent(const ResizeSectionEvent& e);
    ChangeFlags handleEvent(const SelectSectionEvent& e);

    ChangeFlags handleEvent(const ViewportResizedEvent& e);
    ChangeFlags handleEvent(const SetTimelineLengthEvent& e);

    // ===== Notification Helpers =====

    void notifyListeners(ChangeFlags changes);

    // ===== Helper methods =====

    void clampScrollPosition();
    double clampZoom(double zoom) const;

    // Static instance for global access
    static inline TimelineController* currentInstance_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineController)
};

// Helper operators for ChangeFlags
inline TimelineController::ChangeFlags operator|(TimelineController::ChangeFlags a,
                                                 TimelineController::ChangeFlags b) {
    return static_cast<TimelineController::ChangeFlags>(static_cast<uint32_t>(a) |
                                                        static_cast<uint32_t>(b));
}

inline TimelineController::ChangeFlags operator&(TimelineController::ChangeFlags a,
                                                 TimelineController::ChangeFlags b) {
    return static_cast<TimelineController::ChangeFlags>(static_cast<uint32_t>(a) &
                                                        static_cast<uint32_t>(b));
}

inline bool hasFlag(TimelineController::ChangeFlags flags, TimelineController::ChangeFlags flag) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

}  // namespace magda
