#pragma once

#include "../../common/DraggableValueLabel.hpp"
#include "BaseInspector.hpp"
#include "core/AutomationManager.hpp"
#include "core/ParameterInfo.hpp"
#include "core/SelectionManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Inspector for automation curve points
 *
 * Edits the selected automation point(s) for the current
 * AutomationPointSelection:
 *   - Value  : keyboard / drag, shown in the lane's real units
 *   - Pos    : beat position (single point only)
 *   - Curve  : Linear / Curve / Step for the segment after the point
 *   - Corner : "Straighten" clears the segment bend (sharp Linear corner)
 *
 * Multi-selection edits the value by delta (like NoteInspector). All edits go
 * through UndoManager. Selection is pushed in by InspectorContainer; the
 * inspector listens to AutomationManager for live data refreshes.
 */
class AutomationPointInspector : public BaseInspector, private magda::AutomationManagerListener {
  public:
    AutomationPointInspector();
    ~AutomationPointInspector() override;

    void onActivated() override;
    void onDeactivated() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setSelectedPoints(const magda::AutomationPointSelection& selection);

  private:
    magda::AutomationPointSelection selection_;
    magda::ParameterInfo info_;  // for the selected lane's target

    juce::Label countLabel_;
    juce::Label valueLabel_;
    std::unique_ptr<magda::DraggableValueLabel> valueValue_;
    juce::Label posLabel_;
    std::unique_ptr<magda::DraggableValueLabel> posValue_;

    // Drag-start tracking for delta edits (real units / beats).
    double valueDragStart_ = 0.0;
    double posDragStart_ = 0.0;

    // The lane's point list for the current selection (absolute lane or clip).
    const std::vector<magda::AutomationPoint>* sourcePoints() const;
    const magda::AutomationPoint* findPoint(magda::AutomationPointId id) const;

    void updateFromSelection();
    void showControls(bool show);
    void refreshDisplay();

    double normToReal(double normalized) const;
    double realToNorm(double real) const;

    // AutomationManagerListener
    void automationLanesChanged() override {}
    void automationPointsChanged(magda::AutomationLaneId laneId) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomationPointInspector)
};

}  // namespace magda::daw::ui
