#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "AutomationLaneHeader.hpp"
#include "core/AutomationManager.hpp"
#include "core/TypeIds.hpp"

namespace magda {

class AutomationLaneComponent;

/**
 * @brief Visible automation lanes belonging to the master channel
 *        (ControlTarget::Kind::*, trackId == MASTER_TRACK_ID), in order.
 */
std::vector<AutomationLaneId> visibleMasterAutomationLanes();

/**
 * @brief Total pixel height of the master automation band (sum of visible
 *        master lane heights). 0 when the master has no visible lanes, which
 *        collapses the band in MainView's layout.
 */
int masterAutomationBandHeight(double verticalZoom);

/**
 * @brief Fixed left-column header for the master automation band: paints each
 *        master lane's header (name + value ticks) and hosts its snap / bypass /
 *        delete buttons. Mirrors TrackHeadersPanel's per-lane headers via the
 *        shared AutomationLaneHeader helpers, but for the master channel.
 */
class MasterAutomationHeaderPanel : public juce::Component, public AutomationManagerListener {
  public:
    MasterAutomationHeaderPanel();
    ~MasterAutomationHeaderPanel() override;

    void setVerticalZoom(double zoom);

    void paint(juce::Graphics& g) override;
    void resized() override;

    // AutomationManagerListener
    void automationLanesChanged() override;
    void automationLanePropertyChanged(AutomationLaneId laneId) override;

  private:
    void rebuildButtons();
    void layoutButtons();

    std::vector<std::unique_ptr<AutoLaneHeaderButtons>> buttons_;
    double verticalZoom_ = 1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterAutomationHeaderPanel)
};

/**
 * @brief Scrolling content for the master automation band: hosts an
 *        AutomationLaneComponent (curve editor) per master lane, stacked
 *        vertically and sized to the full timeline width so the enclosing
 *        viewport scrolls it horizontally in lock-step with the arrangement.
 */
class MasterAutomationContentPanel : public juce::Component, public AutomationManagerListener {
  public:
    MasterAutomationContentPanel();
    ~MasterAutomationContentPanel() override;

    void setPixelsPerBeat(double pixelsPerBeat);
    void setTempoBPM(double bpm);
    void setVerticalZoom(double zoom);
    void setTimelineWidth(int widthPx);

    // Fired when the band's total height changes (lane added/removed/resized)
    // so MainView can re-run its arrangement layout.
    std::function<void()> onBandHeightChanged;

    // Beat-grid snapping, wired from MainView so master-band lanes (tempo,
    // master volume) snap their edits to the grid like per-track lanes do.
    std::function<double(double)> snapBeatToGrid;
    std::function<double()> getGridSpacingBeats;

    void resized() override;

    // AutomationManagerListener
    void automationLanesChanged() override;
    void automationLanePropertyChanged(AutomationLaneId laneId) override;

  private:
    void rebuildLanes();
    void layoutLanes();

    struct LaneEntry {
        AutomationLaneId laneId = INVALID_AUTOMATION_LANE_ID;
        std::unique_ptr<AutomationLaneComponent> component;
    };
    std::vector<LaneEntry> lanes_;

    double pixelsPerBeat_ = 100.0;
    double tempoBPM_ = 120.0;
    double verticalZoom_ = 1.0;
    int timelineWidth_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterAutomationContentPanel)
};

}  // namespace magda
