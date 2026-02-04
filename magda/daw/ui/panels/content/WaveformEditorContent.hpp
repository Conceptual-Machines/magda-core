#pragma once

#include <memory>

#include "PanelContent.hpp"
#include "core/ClipDisplayInfo.hpp"
#include "core/ClipManager.hpp"
#include "ui/components/timeline/TimeRuler.hpp"
#include "ui/components/waveform/WaveformGridComponent.hpp"
#include "ui/state/TimelineController.hpp"

namespace magda::daw::ui {

/**
 * @brief Waveform editor for audio clips
 *
 * Container that manages:
 * - ScrollNotifyingViewport (scrolling)
 * - WaveformGridComponent (scrollable waveform content)
 * - TimeRuler (synchronized with scroll)
 * - ABS/REL mode toggle
 * - Zoom controls
 *
 * Architecture based on PianoRollContent pattern.
 */
class WaveformEditorContent : public PanelContent,
                              public magda::ClipManagerListener,
                              public TimelineStateListener,
                              public juce::Timer {
  public:
    WaveformEditorContent();
    ~WaveformEditorContent() override;

    PanelContentType getContentType() const override {
        return PanelContentType::WaveformEditor;
    }

    PanelContentInfo getContentInfo() const override {
        return {PanelContentType::WaveformEditor, "Waveform", "Audio waveform editor", "Waveform"};
    }

    void paint(juce::Graphics& g) override;
    void resized() override;

    void onActivated() override;
    void onDeactivated() override;

    // Mouse interaction
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;

    // ClipManagerListener
    void clipsChanged() override;
    void clipPropertyChanged(magda::ClipId clipId) override;
    void clipSelectionChanged(magda::ClipId clipId) override;

    // TimelineStateListener
    void timelineStateChanged(const TimelineState& state) override;
    void playheadStateChanged(const TimelineState& state) override;

    // Set the clip to edit
    void setClip(magda::ClipId clipId);
    magda::ClipId getEditingClipId() const {
        return editingClipId_;
    }

    // Timeline mode
    void setRelativeTimeMode(bool relative);
    bool isRelativeTimeMode() const {
        return relativeTimeMode_;
    }

  private:
    magda::ClipId editingClipId_ = magda::INVALID_CLIP_ID;

    // Timeline mode
    bool relativeTimeMode_ = false;  // false = absolute (timeline), true = relative (clip)

    // Zoom
    double horizontalZoom_ = 100.0;  // pixels per second
    double verticalZoom_ = 1.0;      // amplitude multiplier
    double cachedBpm_ = 120.0;       // last known BPM for zoom scaling on tempo change
    static constexpr double MIN_ZOOM = 20.0;
    static constexpr double MAX_ZOOM = 500.0;
    static constexpr double MIN_VERTICAL_ZOOM = 0.25;
    static constexpr double MAX_VERTICAL_ZOOM = 4.0;

    // Layout constants
    static constexpr int TIME_RULER_HEIGHT = 30;
    static constexpr int TOOLBAR_HEIGHT = 30;
    static constexpr int GRID_LEFT_PADDING = 10;

    // Components (created in constructor)
    class ScrollNotifyingViewport;  // Forward declaration
    std::unique_ptr<ScrollNotifyingViewport> viewport_;
    std::unique_ptr<WaveformGridComponent> gridComponent_;
    std::unique_ptr<magda::TimeRuler> timeRuler_;
    std::unique_ptr<juce::TextButton> timeModeButton_;
    std::unique_ptr<juce::TextButton> warpModeButton_;
    std::unique_ptr<juce::Label> bpmLabel_;
    std::unique_ptr<juce::ComboBox> gridResolutionCombo_;

    // Playhead overlay
    class PlayheadOverlay;
    std::unique_ptr<PlayheadOverlay> playheadOverlay_;
    double cachedEditPosition_ = 0.0;
    double cachedPlaybackPosition_ = 0.0;
    bool cachedIsPlaying_ = false;

    // Look and feel
    class ButtonLookAndFeel;
    std::unique_ptr<ButtonLookAndFeel> buttonLookAndFeel_;

    // Update grid size when clip or zoom changes
    void updateGridSize();

    // Scroll to show clip start
    void scrollToClipStart();

    // Anchor-point zoom
    void performAnchorPointZoom(double zoomFactor, int anchorX);

    // Update the grid's loop boundary from clip info
    void updateDisplayInfo(const magda::ClipInfo& clip);

    // Warp marker helpers
    void refreshWarpMarkers();
    magda::AudioBridge* getBridge();

    // Warp state tracking
    bool wasWarpEnabled_ = false;

    // Transient detection polling
    bool transientsCached_ = false;
    void timerCallback() override;

    // Header drag-zoom state
    bool headerDragActive_ = false;
    int headerDragStartX_ = 0;
    double headerDragStartZoom_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformEditorContent)
};

}  // namespace magda::daw::ui
