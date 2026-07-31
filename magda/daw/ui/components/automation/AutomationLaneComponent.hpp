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
 * Handles coordinate conversion: beat <-> pixel, value <-> Y.
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
    void modifierKeysChanged(const juce::ModifierKeys& modifiers) override;
    void resized() override;

    // Mouse interaction
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
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
    void setPixelsPerBeat(double ppb);
    void setTempoBPM(double bpm);

    /**
     * @brief Show a ghost outline where a dragged copy would land.
     *
     * Used by a clip's copy-drag: the clip itself stays put, and this marks
     * the destination. Painted over the clip components rather than behind
     * them, so the ghost stays visible when it overlaps an existing clip -
     * which is exactly when the user most needs to see where it is going.
     */
    void setClipCopyGhost(double startBeat, double lengthBeats);
    void clearClipCopyGhost();

    // Place the resize handle on the top edge (and invert the drag direction)
    // instead of the bottom. Used by bottom-anchored hosts such as the master
    // automation band, which grows upward, so the grab edge tracks the growth.
    void setResizeHandleAtTop(bool atTop);

    // Height management
    int getPreferredHeight() const;
    bool isExpanded() const;

    // Snapping
    std::function<double(double)> snapBeatToGrid;
    std::function<double()> getGridSpacingBeats;

    // Open a clip's curve in the big editor (bottom panel). Fired by
    // double-clicking an automation clip or its "Edit Curve" menu item.
    std::function<void(AutomationLaneId, AutomationClipId)> onOpenClipEditor;

    // Header dimensions
    static constexpr int HEADER_HEIGHT = 24;
    static constexpr int MIN_LANE_HEIGHT = 40;
    static constexpr int MAX_LANE_HEIGHT = 300;
    static constexpr int DEFAULT_LANE_HEIGHT = 60;
    static constexpr int RESIZE_HANDLE_HEIGHT = 5;
    static constexpr int SCALE_LABEL_WIDTH =
        LayoutConfig::TIMELINE_LEFT_PADDING;  // Left margin for Y-axis scale labels

    // Callbacks for parent coordination
    std::function<void(AutomationLaneId, int)> onHeightChanged;
    std::function<void(AutomationLaneId, double, double)> onTimeSelectionChanged;

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
    /**
     * @brief Draw the unit value scale (dB / pan / discrete / unit labels)
     *        for a target into `area`. normToYOffset maps a normalized value
     *        to a y offset within the area. Shared with the bottom-panel
     *        clip editor's scale strip.
     */
    static void paintScaleLabelsFor(juce::Graphics& g, juce::Rectangle<int> area,
                                    const ControlTarget& target,
                                    const std::function<int(double)>& normToYOffset);

    static void simplifyLane(AutomationLaneId laneId, double epsilon,
                             const std::vector<AutomationPointId>& pointIdFilter = {});

  private:
    AutomationLaneId laneId_;
    double pixelsPerBeat_ = 10.0;
    double tempoBPM_ = 120.0;
    bool isSelected_ = false;
    bool resizeHandleAtTop_ = false;

    // Resize state
    bool isResizing_ = false;
    int resizeStartScreenY_ = 0;
    int resizeStartHeight_ = 0;

    // Header strip time-selection state
    bool isCreatingTimeSelection_ = false;
    juce::Point<int> timeSelectionAnchor_;
    double timeSelectionStartBeat_ = 0.0;

    // Alt+drag clip pencil state (clip-based lanes, empty area)
    bool isDrawingClip_ = false;
    double drawClipStartBeat_ = 0.0;
    double drawClipEndBeat_ = 0.0;

    // Destination outline for a clip copy-drag, owned here rather than by the
    // dragged clip because it has to be able to paint outside that clip's
    // bounds.
    bool hasCopyGhost_ = false;
    double copyGhostStartBeat_ = 0.0;
    double copyGhostLengthBeats_ = 0.0;

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

    // Y of the header strip's top edge: below the resize handle in top-handle
    // mode, otherwise the component's top.
    int headerTop() const {
        return resizeHandleAtTop_ ? RESIZE_HANDLE_HEIGHT : 0;
    }

    // Resize helpers
    bool isInResizeArea(int y) const;
    bool isInTimeSelectionStrip(int x, int y) const;
    // Empty clip-lane area where the Alt pencil can draw a clip.
    bool canDrawClipAt(int x, int y) const;
    double xToBeat(int x) const;
    juce::Rectangle<int> getResizeHandleArea() const;

    // Scale label helpers
    void paintScaleLabels(juce::Graphics& g, juce::Rectangle<int> area);
    juce::String formatScaleValue(double normalizedValue) const;
    int valueToPixel(double value, int areaHeight) const;
};

}  // namespace magda
