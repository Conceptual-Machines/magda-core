#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "../../components/automation/AutomationCurveEditor.hpp"
#include "../../components/timeline/TimeRuler.hpp"
#include "PanelContent.hpp"
#include "core/AutomationManager.hpp"
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
                                    private magda::AutomationManagerListener {
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

    void paint(juce::Graphics& g) override;
    void resized() override;

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
    void refreshFromSelection();
    void rebuildEditor();
    void updateView();
    void layoutCanvas();
    void performAnchorPointZoom(double newZoom, double anchorTime, int anchorScreenX);

    // SelectionManagerListener
    void selectionTypeChanged(magda::SelectionType newType) override;
    void automationClipSelectionChanged(const magda::AutomationClipSelection& selection) override;

    // AutomationManagerListener
    void automationLanesChanged() override;
    void automationClipsChanged(magda::AutomationLaneId laneId) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomationClipEditorContent)
};

}  // namespace magda::daw::ui
