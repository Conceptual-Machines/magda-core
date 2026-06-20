#pragma once

#include <functional>
#include <limits>
#include <memory>
#include <vector>

#include "PanelContent.hpp"
#include "core/ClipManager.hpp"
#include "core/GestureRouter.hpp"
#include "ui/components/pianoroll/PitchFoldMap.hpp"
#include "ui/layout/LayoutConfig.hpp"
#include "ui/state/TimelineController.hpp"
#include "ui/state/TimelineState.hpp"

namespace magda {
class TimeRuler;
class VelocityLaneComponent;
class MidiDrawerComponent;
class MidiBridge;
struct MidiNoteEvent;
}  // namespace magda

namespace magda::daw::ui {

class VerticalZoomStrip : public juce::Component {
  public:
    VerticalZoomStrip(int minValue, int maxValue);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;

    void setGestureContext(magda::GestureContext context) {
        gestureContext_ = context;
    }

    std::function<int()> getValue;
    std::function<void(int, int)> onZoomChanged;  // newValue, anchorScreenY

  private:
    magda::GestureContext gestureContext_ = magda::GestureContext::PianoRoll;
    int minValue_ = 1;
    int maxValue_ = 1;
    int mouseDownX_ = 0;
    int mouseDownY_ = 0;
    int startValue_ = 1;
    int lastSentValue_ = 1;
    bool dragging_ = false;
};

/**
 * @brief Custom viewport that fires a callback on scroll and repaints registered components.
 *
 * Replaces the separate ScrollNotifyingViewport and DrumGridScrollViewport classes.
 */
class MidiEditorViewport : public juce::Viewport {
  public:
    std::function<void(int, int)> onScrolled;
    std::vector<juce::Component*> componentsToRepaint;

    bool keyPressed(const juce::KeyPress& key) override {
        // Alt/Option + arrow keys: allow viewport scrolling
        // Plain arrow keys: don't handle, let grid component use them for note movement
        if (key.getKeyCode() == juce::KeyPress::upKey ||
            key.getKeyCode() == juce::KeyPress::downKey ||
            key.getKeyCode() == juce::KeyPress::leftKey ||
            key.getKeyCode() == juce::KeyPress::rightKey) {
            if (key.getModifiers().isAltDown())
                return juce::Viewport::keyPressed(key);
            return false;
        }
        return juce::Viewport::keyPressed(key);
    }

    void visibleAreaChanged(const juce::Rectangle<int>& newVisibleArea) override {
        juce::Viewport::visibleAreaChanged(newVisibleArea);
        if (onScrolled)
            onScrolled(getViewPositionX(), getViewPositionY());
        for (auto* c : componentsToRepaint)
            if (c)
                c->repaint();
    }

    void scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) override {
        juce::Viewport::scrollBarMoved(scrollBar, newRangeStart);
        for (auto* c : componentsToRepaint)
            if (c)
                c->repaint();
    }
};

/**
 * @brief Shared base class for MIDI editor content panels (PianoRoll and DrumGrid).
 *
 * Provides common zoom, scroll, TimeRuler, and listener management.
 * Subclasses implement their own grid component, layout, and editor-specific features.
 *
 * Inheritance hierarchy:
 *   PanelContent
 *     -> MidiEditorContent (shared zoom, scroll, TimeRuler, listeners)
 *          -> PianoRollContent (keyboard, velocity, chord row, multi-clip)
 *          -> DrumGridClipContent (row labels, pad model, drum grid plugin)
 */
class MidiEditorContent : public PanelContent,
                          public magda::ClipManagerListener,
                          public magda::TimelineStateListener {
  public:
    MidiEditorContent();
    ~MidiEditorContent() override;

    magda::ClipId getEditingClipId() const {
        return editingClipId_;
    }

    bool isRelativeTimeMode() const {
        return relativeTimeMode_;
    }

    // Timeline mode
    virtual void setRelativeTimeMode(bool relative);

    // Per-clip grid settings
    void applyClipGridSettings();
    void setGridSettingsFromUI(bool autoGrid, int numerator, int denominator);
    void setSnapEnabledFromUI(bool enabled);

    // ClipManagerListener — default implementations
    void clipsChanged() override;
    void clipPropertyChanged(magda::ClipId clipId) override;

    // TimelineStateListener — shared implementation
    void timelineStateChanged(const magda::TimelineState& state,
                              magda::ChangeFlags changes) override;

    // --- Multi-track overlay (ghost notes from other tracks, #1281) ---
    // Shared between piano roll and drum grid; each editor renders the
    // overlay in its own grid via applyOverlayTracks().
    bool hasOverlayTracks() const {
        return !overlayTrackIds_.empty();
    }
    // Sticky multi-select menu of other MIDI tracks (anchored at `anchor`);
    // onChanged fires after every overlay change (for button lit-state sync)
    void showOverlayTracksMenu(juce::Component* anchor, std::function<void()> onChanged);

  protected:
    // --- Shared state ---
    magda::ClipId editingClipId_ = magda::INVALID_CLIP_ID;
    double horizontalZoom_ = 50.0;  // pixels per beat
    bool relativeTimeMode_ = false;
    double lastScrolledPlacementStartBeat_ = std::numeric_limits<double>::quiet_NaN();

    // --- Grid resolution (from BottomPanel grid controls) ---
    double gridResolutionBeats_ = 0.25;  // Current grid resolution in beats (default 1/16)
    bool snapEnabled_ = true;            // Whether snap-to-grid is active

    double getGridResolutionBeats() const {
        return gridResolutionBeats_;
    }
    double snapBeatToGrid(double beat) const;
    void updateGridResolution();

    // --- Layout constants ---
    // A bit taller so the bar numbers + loop markers don't look crammed.
    static constexpr int RULER_HEIGHT = 42;
    // Single source in LayoutConfig so the piano-roll and drum-grid bodies +
    // ruler share one padding and can't drift (leaves room for the bar-1
    // playhead triangle clear of the left column).
    static constexpr int GRID_LEFT_PADDING = magda::LayoutConfig::MIDI_GRID_LEFT_PADDING;
    static constexpr double MIN_HORIZONTAL_ZOOM = 10.0;
    static constexpr double MAX_HORIZONTAL_ZOOM = 500.0;
    static constexpr int DEFAULT_DRAWER_HEIGHT = 100;
    static constexpr int MIN_DRAWER_HEIGHT = 60;
    static constexpr int MAX_DRAWER_HEIGHT = 400;
    static constexpr int VELOCITY_LANE_HEIGHT = 80;
    static constexpr int VELOCITY_HEADER_HEIGHT = 20;
    int drawerHeight_ = DEFAULT_DRAWER_HEIGHT;

    // --- Components (accessible to subclasses) ---
    std::unique_ptr<MidiEditorViewport> viewport_;
    std::unique_ptr<magda::TimeRuler> timeRuler_;
    std::unique_ptr<magda::VelocityLaneComponent> velocityLane_;
    std::unique_ptr<magda::MidiDrawerComponent> midiDrawer_;

    // --- Overlay track state (static so it persists across editor switches;
    //     transient per app run) ---
    static std::vector<magda::TrackId> overlayTrackIds_;
    // Push overlayTrackIds_ into the editor's grid renderer
    virtual void applyOverlayTracks() {}

    // --- Lane drawer state (static so it persists across editor switches) ---
    // velocityLaneVisible_ is the velocity toggle; velocityDrawerOpen_ is the
    // derived "drawer area shown" (velocity visible OR a CC lane exists), so the
    // velocity and CC lanes toggle independently — opening CC no longer forces
    // the velocity lane.
    static bool velocityDrawerOpen_;
    static bool velocityLaneVisible_;
    void setVelocityDrawerVisible(bool visible);
    bool isVelocityDrawerVisible() const {
        return velocityDrawerOpen_;
    }
    // Push the velocity-visible flag into the drawer, recompute drawer-open, and
    // relayout. Called by the velocity toggle and on CC lane add/remove.
    void refreshLaneDrawer();
    // Subclass updates its sidebar toggle active states (velocity + CC).
    virtual void updateLaneToggleStates() {}

    // --- Fold (shared): collapse the vertical axis to the clip's used pitches
    //     (piano roll: used notes; drum grid: used pads). foldEnabled_ is static
    //     so the toggle persists across editor/clip switches within a session
    //     (transient, not serialized). Subclasses wire their grid/keyboard/row
    //     components to &foldMap_ once and repaint via onFoldMapChanged(). ---
    static bool foldEnabled_;
    magda::PitchFoldMap foldMap_;
    void rebuildFoldMap();
    void applyFold();
    // Default gathers the editing clip's note numbers; the piano roll overrides
    // to union across a multi-clip selection.
    virtual std::vector<int> collectUsedPitches() const;
    // Subclass repaints its fold-aware components (grid + left column).
    virtual void onFoldMapChanged() {}
    // Subclass scrolls the used rows back into view after a fold toggle.
    virtual void recenterOnNotes() {}

    // --- Loop drag state (visual preview during drag, commit on mouseUp) ---
    bool draggingLoopRegion_ = false;
    double previewLoopStartBeats_ = 0.0;
    double previewLoopLengthBeats_ = 0.0;

    // --- Shared zoom methods ---
    void performAnchorPointZoom(double newZoom, double anchorTime, int anchorScreenX);
    void performWheelZoom(double zoomFactor, int mouseXInViewport);
    void zoomToTimeRange(double startTime, double endTime);

    // --- Shared TimeRuler method (virtual for subclass extension) ---
    virtual void updateTimeRuler();
    void scrollToClipStartForTimeMode();

    // --- Pure virtual methods for subclasses ---
    virtual int getLeftPanelWidth() const = 0;
    virtual void updateGridSize() = 0;
    virtual void setGridPixelsPerBeat(double ppb) = 0;
    virtual void setGridPlayheadPosition(double position) = 0;

    // --- Edit cursor (subclass must forward to its grid component) ---
    virtual void setGridEditCursorPosition(double positionSeconds, bool visible) = 0;

    // --- Optional virtual hooks ---
    virtual void onScrollPositionChanged(int /*scrollX*/, int /*scrollY*/) {}
    virtual void onGridResolutionChanged() {}
    virtual void updateGridLoopRegion() {}
    virtual void setGridPhasePreview(double /*beats*/, bool /*active*/) {}

    // --- Live MIDI note monitor ---
    // Highlights notes as they play while a clip is open, by chaining onto the
    // MidiBridge note callback. Shared by the piano roll (keyboard) and the drum
    // grid (pad rows); subclasses implement the highlight surface via the hooks
    // below. Install on activation, uninstall on deactivation/destruction.
    void installMidiNoteMonitor();
    void uninstallMidiNoteMonitor();

    // Subclass hooks fired for a played note that passes the monitor gate (clip
    // matches the played track and the track has input monitoring on). Default
    // no-ops so a subclass can opt into either independently.
    virtual void highlightMonitoredNote(int /*noteNumber*/, bool /*noteOn*/) {}
    virtual void ensureMonitoredNoteVisible(int /*noteNumber*/) {}

  private:
    // Applies the monitor gate (clip/track match + input monitoring) then fans
    // out to the highlight hooks. Invoked from the chained MidiBridge callback.
    void handleMidiNoteEvent(magda::TrackId trackId, const magda::MidiNoteEvent& event);

    magda::MidiBridge* monitoredMidiBridge_ = nullptr;
    std::function<void(magda::TrackId, const magda::MidiNoteEvent&)> previousMidiNoteCallback_;
    bool midiNoteMonitorInstalled_ = false;

  protected:
    // --- Velocity lane methods (legacy, used by velocity-only path) ---
    void setupVelocityLane();
    virtual void updateVelocityLane();
    virtual void onVelocityEdited();
    void setVelocityLaneSelectedNotes(const std::vector<size_t>& indices);

    // --- MIDI drawer methods (stacked lanes: velocity + CC + pitchbend) ---
    void setupMidiDrawer();
    virtual void updateMidiDrawer();

    // Helper to get current drawer height
    int getDrawerHeight() const {
        return velocityDrawerOpen_ ? drawerHeight_ : 0;
    }

    // --- Edit cursor (local to MIDI editor, independent from arrangement) ---
    void setLocalEditCursor(double positionSeconds);
    double localEditCursorPosition_ = -1.0;  // seconds, -1 = hidden
    bool editCursorBlinkVisible_ = true;

    // Inner timer for edit cursor blink (avoids juce::Timer diamond with subclasses)
    class BlinkTimer : public juce::Timer {
      public:
        std::function<void()> callback;
        void timerCallback() override {
            if (callback)
                callback();
        }
    };
    BlinkTimer blinkTimer_;

  public:
    // Callback for BottomPanel to update num/den display when auto-grid changes
    std::function<void(int numerator, int denominator)> onAutoGridDisplayChanged;
};

}  // namespace magda::daw::ui
