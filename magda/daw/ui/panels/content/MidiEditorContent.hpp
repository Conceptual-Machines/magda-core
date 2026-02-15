#pragma once

#include <memory>
#include <vector>

#include "PanelContent.hpp"
#include "core/ClipManager.hpp"
#include "ui/state/TimelineController.hpp"
#include "ui/state/TimelineState.hpp"

namespace magda {
class TimeRuler;
}

namespace magda::daw::ui {

/**
 * @brief Custom viewport that fires a callback on scroll and repaints registered components.
 *
 * Replaces the separate ScrollNotifyingViewport and DrumGridScrollViewport classes.
 */
class MidiEditorViewport : public juce::Viewport {
  public:
    std::function<void(int, int)> onScrolled;
    std::vector<juce::Component*> componentsToRepaint;

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

  protected:
    // --- Shared state ---
    magda::ClipId editingClipId_ = magda::INVALID_CLIP_ID;
    double horizontalZoom_ = 50.0;  // pixels per beat
    bool relativeTimeMode_ = false;

    // --- Grid resolution (from BottomPanel grid controls) ---
    double gridResolutionBeats_ = 0.25;  // Current grid resolution in beats (default 1/16)
    bool snapEnabled_ = true;            // Whether snap-to-grid is active

    double getGridResolutionBeats() const {
        return gridResolutionBeats_;
    }
    double snapBeatToGrid(double beat) const;
    void updateGridResolution();

    // --- Layout constants ---
    static constexpr int RULER_HEIGHT = 36;
    static constexpr int GRID_LEFT_PADDING = 2;
    static constexpr double MIN_HORIZONTAL_ZOOM = 10.0;
    static constexpr double MAX_HORIZONTAL_ZOOM = 500.0;

    // --- Components (accessible to subclasses) ---
    std::unique_ptr<MidiEditorViewport> viewport_;
    std::unique_ptr<magda::TimeRuler> timeRuler_;

    // --- Shared zoom methods ---
    void performAnchorPointZoom(double newZoom, double anchorTime, int anchorScreenX);
    void performWheelZoom(double zoomFactor, int mouseXInViewport);

    // --- Shared TimeRuler method (virtual for subclass extension) ---
    virtual void updateTimeRuler();

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

    // --- Edit cursor blink state ---
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
