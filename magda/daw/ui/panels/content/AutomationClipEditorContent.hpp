#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "../../components/automation/AutomationCurveEditor.hpp"
#include "../../components/timeline/TimeRuler.hpp"
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
                                    private magda::ClipManagerListener {
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

    // Header grid controls (BottomPanel's shared AUTO/SNAP + num/den set).
    // Grid settings live on the automation clip, like MIDI clips.
    magda::AutomationClipId getEditingClipId() const {
        return selection_.clipId;
    }
    void setGridSettingsFromUI(bool autoGrid, int numerator, int denominator);
    void setSnapEnabledFromUI(bool enabled);
    // Fires when auto-grid recomputes from zoom so the num/den labels follow.
    std::function<void(int numerator, int denominator)> onAutoGridDisplayChanged;
    // Fires on any clip change so BottomPanel can resync its header controls
    // (loop toggle, grid labels) when the inspector or undo edits the clip.
    std::function<void()> onClipStateChanged;
    // Re-emits the current grid state; BottomPanel calls this right after
    // wiring onAutoGridDisplayChanged (the initial updateView ran before the
    // callback existed, so auto mode would show the clip's stored num/den).
    void refreshGridDisplay() {
        gridSettingsChanged();
    }

  private:
    magda::AutomationClipSelection selection_;

    juce::Label titleLabel_;
    std::unique_ptr<magda::TimeRuler> timeRuler_;
    juce::Viewport viewport_;
    juce::Component canvas_;
    std::unique_ptr<magda::AutomationCurveEditor> editor_;

    double horizontalZoom_ = 0.0;  // pixels per beat; 0 = fit on next layout

    const magda::AutomationClipInfo* getClip() const;
    double viewSpanBeats(const magda::AutomationClipInfo& clip) const;
    double gridResolutionBeats() const;
    void gridSettingsChanged();
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomationClipEditorContent)
};

}  // namespace magda::daw::ui
