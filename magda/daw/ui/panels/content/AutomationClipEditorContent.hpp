#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "../../components/automation/AutomationCurveEditor.hpp"
#include "../../components/automation/AutomationLaneComponent.hpp"
#include "../../components/common/DraggableValueLabel.hpp"
#include "../../components/timeline/TimeRuler.hpp"
#include "../../state/TimelineController.hpp"
#include "../../themes/SmallButtonLookAndFeel.hpp"
#include "PanelContent.hpp"
#include "core/AutomationManager.hpp"
#include "core/ClipManager.hpp"
#include "core/SelectionManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Bottom-panel editor for an automation clip (issue #1087): the
 *        lane's curve editor blown up to full panel size.
 *
 * Shown when an automation clip is selected (same selection-driven switch
 * as the piano roll for MIDI clips). Two views, mirroring the MIDI editor's
 * time modes:
 *   looped     -> the clip's own timeline: one loop cycle from bar 1.
 *   not looped -> the real timeline: points, grid, and ruler at the clip's
 *                 actual arrangement position.
 *
 * The curve sits on a viewport-hosted canvas; the ruler drives anchor-point
 * zoom and horizontal scroll like every other editor. Fundamental clip
 * properties (start / end / loop) live in the inspector.
 */
class AutomationClipEditorContent : public PanelContent,
                                    private magda::SelectionManagerListener,
                                    private magda::AutomationManagerListener,
                                    private magda::ClipManagerListener,
                                    private magda::TimelineStateListener {
  public:
    AutomationClipEditorContent();
    ~AutomationClipEditorContent() override;

    PanelContentType getContentType() const override {
        return PanelContentType::AutomationClipEditor;
    }
    PanelContentInfo getContentInfo() const override {
        return {PanelContentType::AutomationClipEditor, "Automation",
                "Automation clip curve editor", "Automation"};
    }

    void onActivated() override;
    void onDeactivated() override;

    bool wantsHeader() const override {
        return true;
    }

    void paint(juce::Graphics& g) override;
    void resized() override;

    magda::AutomationClipId getEditingClipId() const {
        return selection_.clipId;
    }
    // Fires on any clip change so BottomPanel can resync its header controls
    // (the shared loop toggle) when the inspector or undo edits the clip.
    std::function<void()> onClipStateChanged;

    // Content-owned header controls: SNAP X (time grid) and SNAP Y (value
    // grid), each with its own num/den. Settings live on the clip.
    void populateHeader(juce::Component& headerBar) override;
    void depopulateHeader(juce::Component& headerBar) override;
    void layoutHeader(juce::Rectangle<int> headerBounds) override;

  private:
    magda::AutomationClipSelection selection_;

    juce::Label titleLabel_;
    std::unique_ptr<magda::TimeRuler> timeRuler_;
    juce::Viewport viewport_;
    juce::Component canvas_;
    std::unique_ptr<magda::AutomationCurveEditor> editor_;

    double horizontalZoom_ = 0.0;          // pixels per beat; 0 = fit on next layout
    juce::Rectangle<int> scaleStripArea_;  // left value scale, next to the viewport

    // Header snap controls (reparented into the panel header bar)
    SmallButtonLookAndFeel smallButtonLF_;
    std::unique_ptr<juce::TextButton> snapXButton_, snapYButton_;
    std::unique_ptr<magda::DraggableValueLabel> snapXNum_, snapXDen_, snapYNum_, snapYDen_;
    juce::Label snapXSlash_, snapYSlash_;

    const magda::AutomationClipInfo* getClip() const;
    double viewSpanBeats(const magda::AutomationClipInfo& clip) const;
    double gridResolutionBeats() const;
    void gridSettingsChanged();
    void buildHeaderControls();
    void syncSnapControls();
    void updateTitle();
    // Ghost of the lane's track content (piano-roll style MIDI notes /
    // audio waveform) painted beneath the curve via the editor's underlay.
    void paintTrackGhost(juce::Graphics& g);
    void refreshFromSelection();
    void rebuildEditor();
    void updateView();
    void layoutCanvas();
    void performAnchorPointZoom(double newZoom, double anchorTime, int anchorScreenX);

    // SelectionManagerListener
    void selectionTypeChanged(magda::SelectionType newType) override;
    void automationClipSelectionChanged(const magda::AutomationClipSelection& selection) override;
    void automationPointSelectionChanged(const magda::AutomationPointSelection& selection) override;

    // AutomationManagerListener
    void automationLanesChanged() override;
    void automationClipsChanged(magda::AutomationLaneId laneId) override;

    // ClipManagerListener — the track ghost must follow MIDI/audio edits.
    void clipsChanged() override;
    void clipPropertyChanged(magda::ClipId clipId) override;

    // TimelineStateListener — playhead over the curve while playing.
    void timelineStateChanged(const magda::TimelineState& state,
                              magda::ChangeFlags changes) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomationClipEditorContent)
};

}  // namespace magda::daw::ui
