#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "../../layout/LayoutConfig.hpp"
#include "AutomationClipComponent.hpp"
#include "AutomationCurveEditor.hpp"
#include "core/AutomationInfo.hpp"
#include "core/AutomationManager.hpp"
#include "core/SelectionManager.hpp"

namespace magda {

/**
 * @brief Container component for one automation lane
 *
 * Contains a header with name, visibility toggle, arm button.
 * Below header: either CurveEditor (absolute) or ClipComponents (clip-based).
 * Handles coordinate conversion: time <-> pixel, value <-> Y.
 */
class AutomationLaneComponent : public juce::Component,
                                public AutomationManagerListener,
                                public SelectionManagerListener {
  public:
    explicit AutomationLaneComponent(AutomationLaneId laneId);
    ~AutomationLaneComponent() override;

    // Component
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

    // Mouse interaction
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    bool hitTest(int x, int y) override;

    // AutomationManagerListener
    void automationLanesChanged() override;
    void automationLanePropertyChanged(AutomationLaneId laneId) override;
    void automationClipsChanged(AutomationLaneId laneId) override;

    // SelectionManagerListener
    void selectionTypeChanged(SelectionType newType) override;
    void automationLaneSelectionChanged(const AutomationLaneSelection& selection) override;

    // Configuration
    AutomationLaneId getLaneId() const {
        return laneId_;
    }
    void setPixelsPerSecond(double pps);
    void setPixelsPerBeat(double ppb);
    void setTempoBPM(double bpm);
    double getPixelsPerSecond() const {
        return pixelsPerSecond_;
    }

    // Height management
    int getPreferredHeight() const;
    bool isExpanded() const;

    // Snapping
    std::function<double(double)> snapTimeToGrid;
    std::function<double()> getGridSpacingBeats;

    // Header dimensions
    static constexpr int HEADER_HEIGHT = 24;
    static constexpr int MIN_LANE_HEIGHT = 40;
    static constexpr int MAX_LANE_HEIGHT = 200;
    static constexpr int DEFAULT_LANE_HEIGHT = 60;
    static constexpr int RESIZE_HANDLE_HEIGHT = 5;
    static constexpr int SCALE_LABEL_WIDTH =
        LayoutConfig::TIMELINE_LEFT_PADDING;  // Left margin for Y-axis scale labels

    // Callbacks for parent coordination
    std::function<void(AutomationLaneId, int)> onHeightChanged;

    /**
     * @brief Run Ramer–Douglas–Peucker simplification on an absolute automation
     *        lane, wrapped in a single undoable op.
     *
     * @param laneId         Target lane.
     * @param epsilon        Tolerance in normalized value units (e.g. 0.01 =
     *                       1% of parameter range). Points within epsilon of
     *                       the linear interpolation between retained
     *                       neighbours are dropped.
     * @param pointIdFilter  If non-empty, restrict simplification to just
     *                       these point IDs (all other points are left
     *                       untouched). Empty means "simplify the whole lane".
     */
    static void simplifyLane(AutomationLaneId laneId, double epsilon,
                             const std::vector<AutomationPointId>& pointIdFilter = {});

  private:
    AutomationLaneId laneId_;
    double pixelsPerSecond_ = 100.0;
    double pixelsPerBeat_ = 10.0;
    double tempoBPM_ = 120.0;
    bool isSelected_ = false;

    // Resize state
    bool isResizing_ = false;
    int resizeStartY_ = 0;
    int resizeStartHeight_ = 0;

    // UI components
    std::unique_ptr<AutomationCurveEditor> curveEditor_;
    std::vector<std::unique_ptr<AutomationClipComponent>> clipComponents_;

    juce::Label nameLabel_;

    void setupHeader();
    void rebuildContent();
    void rebuildClipComponents();
    void updateClipPositions();
    void syncSelectionState();
    void showContextMenu();

    // Get lane info
    const AutomationLaneInfo* getLaneInfo() const;

    // Resize helpers
    bool isInResizeArea(int y) const;
    juce::Rectangle<int> getResizeHandleArea() const;

    // Scale label helpers
    void paintScaleLabels(juce::Graphics& g, juce::Rectangle<int> area);
    juce::String formatScaleValue(double normalizedValue) const;
    int valueToPixel(double value, int areaHeight) const;
};

}  // namespace magda
