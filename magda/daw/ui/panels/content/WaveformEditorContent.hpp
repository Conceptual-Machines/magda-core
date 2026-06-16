#pragma once

#include <memory>

#include "PanelContent.hpp"
#include "audio/AudioThumbnailManager.hpp"  // TransientCacheListener
#include "core/ClipDisplayInfo.hpp"
#include "core/ClipManager.hpp"
#include "core/UndoManager.hpp"
#include "ui/components/common/DraggableValueLabel.hpp"
#include "ui/components/timeline/TimeRuler.hpp"
#include "ui/components/timeline/ZoomScrollBar.hpp"
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
 * - Source-relative time ruler
 * - Zoom controls
 *
 * Architecture based on PianoRollContent pattern.
 */
class WaveformEditorContent : public PanelContent,
                              private juce::Timer,
                              public magda::ClipManagerListener,
                              public magda::UndoManagerListener,
                              public TimelineStateListener,
                              public magda::TransientCacheListener {
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
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

    void onActivated() override;
    void onDeactivated() override;

    bool wantsHeader() const override {
        return true;
    }

    // Mouse interaction
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& event, float scaleFactor) override;

    // ClipManagerListener
    void clipsChanged() override;
    void clipPropertyChanged(magda::ClipId clipId) override;
    void clipSelectionChanged(magda::ClipId clipId) override;

    // UndoManagerListener
    void undoStateChanged() override;

    // TimelineStateListener
    void timelineStateChanged(const TimelineState& state, ChangeFlags changes) override;

    // TransientCacheListener - the detector recomputed/cleared transients for a
    // file (e.g. sensitivity changed); refresh on the callback instead of polling.
    void transientsChanged(const juce::String& filePath) override;

    // Set the clip to edit
    void setClip(magda::ClipId clipId);
    magda::ClipId getEditingClipId() const {
        return editingClipId_;
    }

    // Waveform editor is always source-relative.
    void setRelativeTimeMode(bool relative);
    bool isRelativeTimeMode() const {
        return true;
    }
    void setSnapEnabledFromUI(bool enabled);

    // Loop-record take lanes (header TAKES toggle).
    bool editingClipHasMultipleTakes() const;
    bool areTakesExpanded() const;
    void setTakesExpanded(bool expanded);

  private:
    magda::ClipId editingClipId_ = magda::INVALID_CLIP_ID;

    bool relativeTimeMode_ = true;

    // Zoom
    double horizontalZoom_ = 100.0;  // pixels per second
    double verticalZoom_ = 1.0;      // amplitude multiplier
    double cachedBpm_ = 120.0;       // last known BPM for zoom scaling on tempo change
    static constexpr double MIN_ZOOM = 5.0;
    static constexpr double MAX_ZOOM = 1e6;  // ~22 px/sample at 44.1kHz — sample-level editing
    static constexpr double MIN_VERTICAL_ZOOM = 0.25;
    static constexpr double MAX_VERTICAL_ZOOM = 4.0;

    // Layout constants
    static constexpr int TIME_RULER_HEIGHT = 48;
    static constexpr int TOOLBAR_HEIGHT = 30;
    static constexpr int GRID_LEFT_PADDING = 10;
    static constexpr int H_SCROLLBAR_HEIGHT = 12;

    // Components (created in constructor)
    class ScrollNotifyingViewport;  // Forward declaration
    std::unique_ptr<ScrollNotifyingViewport> viewport_;
    std::unique_ptr<WaveformGridComponent> gridComponent_;
    std::unique_ptr<magda::TimeRuler> timeRuler_;
    std::unique_ptr<magda::ZoomScrollBar> horizontalScrollBar_;
    std::unique_ptr<juce::TextButton> timeModeButton_;

    std::unique_ptr<DraggableValueLabel> gridNumeratorLabel_;
    std::unique_ptr<DraggableValueLabel> gridDenominatorLabel_;
    std::unique_ptr<juce::Label> gridSlashLabel_;
    std::unique_ptr<juce::TextButton> snapButton_;
    std::unique_ptr<juce::TextButton> gridButton_;
    bool gridVisible_ = true;
    bool snapEnabled_ = false;
    int gridNumerator_ = 1;
    int gridDenominator_ = 4;

    // Playhead overlay
    class PlayheadOverlay;
    std::unique_ptr<PlayheadOverlay> playheadOverlay_;
    double cachedEditPosition_ = 0.0;
    double cachedPlaybackPosition_ = 0.0;
    bool cachedIsPlaying_ = false;
    magda::ClipDisplayInfo cachedDisplayInfo_{};  // Cached for playhead overlay positioning

    // Look and feel
    class ButtonLookAndFeel;
    std::unique_ptr<ButtonLookAndFeel> buttonLookAndFeel_;

    // Virtual scroll position (replaces viewport-based horizontal scrolling)
    int virtualScrollX_ = 0;
    bool isUpdatingFromScrollBar_ = false;

    int getMaxVirtualScrollX() const;
    void setVirtualScrollX(int x);
    void updateHorizontalScrollBar();

    // Update grid size when clip or zoom changes
    void updateGridSize();

    // Scroll to show clip start
    void scrollToClipStart();

    // Anchor-point zoom
    void performAnchorPointZoom(double zoomFactor, int anchorX);
    void zoomToTimeRange(double startTime, double endTime);

    // Update the grid's loop boundary from clip info
    void updateDisplayInfo(const magda::ClipInfo& clip);

    // Warp marker helpers
    void refreshWarpMarkers();
    magda::AudioBridge* getBridge();

    // Slice helpers
    void sliceAtWarpMarkers();
    void sliceAtGrid();
    void sliceWarpMarkersToDrumGrid();
    void sliceAtGridToDrumGrid();

    // Warp state tracking
    bool wasWarpEnabled_ = false;

    // Transient detection
    bool transientsCached_ = false;
    bool transientsUpdating_ = false;
    float transientSpinnerPhase_ = 0.0f;
    void requestTransientDetection();
    void setTransientsUpdating(bool updating);
    void timerCallback() override;

    // Header drag-zoom state
    bool headerDragActive_ = false;
    int headerDragStartX_ = 0;
    int headerDragStartY_ = 0;
    int headerDragAnchorX_ = 0;
    double headerDragStartZoom_ = 0.0;

    // Waveform zoom drag state (from grid component callback)
    double waveformZoomStartZoom_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformEditorContent)
};

}  // namespace magda::daw::ui
