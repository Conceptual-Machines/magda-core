#pragma once

#include "../core/TypeIds.hpp"

namespace magda {

/**
 * Abstract view onto SelectionManager — the subset the agent layer reads.
 *
 * Lives behind MagdaApi so agents/scripting/CLI consumers don't depend on
 * the SelectionManager singleton directly. Kept lightweight on purpose:
 * the abstract header pulls in only TypeIds, not the full singleton +
 * UI-listener machinery from SelectionManager.hpp. Concrete struct types
 * (e.g. AutomationLaneSelection) are deliberately not exposed here —
 * their members are surfaced as primitive accessors instead.
 *
 * PR1 only exposes what AutomationExecutor needs; subsequent PRs grow
 * the surface as more call sites migrate.
 */
class SelectionApi {
  public:
    virtual ~SelectionApi() = default;

    virtual TrackId getSelectedTrack() const = 0;

    /**
     * @return The lane id of the currently selected automation lane, or
     *         INVALID_AUTOMATION_LANE_ID if no automation lane is
     *         selected (e.g. another selection type is active, or
     *         nothing is selected).
     */
    virtual AutomationLaneId getSelectedAutomationLaneId() const = 0;
};

}  // namespace magda
