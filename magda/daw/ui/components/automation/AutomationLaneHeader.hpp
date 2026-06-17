#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "AutomationInfo.hpp"
#include "TypeIds.hpp"

namespace magda {

/**
 * @brief The four per-lane header buttons (snap-edits / snap-value / bypass /
 *        delete) shown to the left of an automation lane.
 *
 * The concrete button classes (custom LaneHeaderButton subclasses) live in the
 * .cpp; this struct only holds them as juce::Button base pointers. Owned by
 * whatever panel hosts the lane header (TrackHeadersPanel for per-track lanes,
 * the master automation band for the master volume lane). They are real child
 * components of that host, so `makeAutoLaneHeaderButtons` adds them to it.
 */
struct AutoLaneHeaderButtons {
    AutomationLaneId laneId = INVALID_AUTOMATION_LANE_ID;
    std::unique_ptr<juce::Button> snapEditGridBtn;
    std::unique_ptr<juce::Button> snapValueBtn;
    std::unique_ptr<juce::Button> bypassBtn;
    std::unique_ptr<juce::Button> deleteBtn;
};

/**
 * @brief Build and wire the header buttons for a lane, parented to `host`.
 *
 * Click handlers capture the laneId by value and route through the global
 * AutomationManager / UndoManager, so the buttons keep working across host
 * rebuilds. Toggle state is synced from the lane in `layoutAutoLaneHeaderButtons`.
 */
std::unique_ptr<AutoLaneHeaderButtons> makeAutoLaneHeaderButtons(AutomationLaneId laneId,
                                                                 juce::Component& host);

/**
 * @brief Sync the buttons' toggle (on/off) visuals from the lane's current
 *        snap/bypass flags. Cheap; call after a rebuild or property change.
 */
void syncAutoLaneHeaderButtonStates(AutoLaneHeaderButtons& buttons, const AutomationLaneInfo& lane);

/**
 * @brief Position the buttons within the lane's content area and sync their
 *        toggle state. Hidden when the lane is collapsed.
 * @param laneTopY Top of the lane (its header row) in the host's coordinates.
 * @param topInset Pixels of grab strip above the header (the master band puts
 *                 its resize handle on the top edge); 0 for bottom-handle hosts.
 */
void layoutAutoLaneHeaderButtons(AutoLaneHeaderButtons& buttons, const AutomationLaneInfo& lane,
                                 int laneTopY, int topInset = 0);

/**
 * @brief Paint a single automation lane header: background, parameter name, and
 *        the value tick marks / labels down the right edge of the header column.
 * @param laneTopY  Top of the lane in the host's coordinates.
 * @param width     Width of the header column (the labels flush to its right edge).
 * @param laneHeight Full lane height (header + curve + resize handle when expanded).
 * @param topInset  Pixels of grab strip above the header (top-handle hosts); 0
 *                  for bottom-handle hosts so per-track lanes are unchanged.
 */
void paintAutomationLaneHeader(juce::Graphics& g, const AutomationLaneInfo& lane, int laneTopY,
                               int width, int laneHeight, int topInset = 0);

}  // namespace magda
