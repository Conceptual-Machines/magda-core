#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

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
    double getPixelsPerSecond() const {
        return pixelsPerSecond_;
    }

    // Height management
    int getPreferredHeight() const;
    bool isExpanded() const;

    // Snapping
    std::function<double(double)> snapTimeToGrid;

    // Header dimensions
    static constexpr int HEADER_HEIGHT = 20;
    static constexpr int MIN_LANE_HEIGHT = 40;
    static constexpr int MAX_LANE_HEIGHT = 200;
    static constexpr int DEFAULT_LANE_HEIGHT = 60;
    static constexpr int RESIZE_HANDLE_HEIGHT = 5;
    static constexpr int SCALE_LABEL_WIDTH =
        18;  // Left margin for Y-axis scale labels (matches TrackContentPanel::LEFT_PADDING)

    // Callbacks for parent coordination
    std::function<void(AutomationLaneId, int)> onHeightChanged;

  private:
    AutomationLaneId laneId_;
    double pixelsPerSecond_ = 100.0;
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
