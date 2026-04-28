#pragma once

#include "../core/SelectionManager.hpp"
#include "../core/TypeIds.hpp"

namespace magda {

/**
 * Abstract view onto SelectionManager — the subset the agent layer reads.
 *
 * Lives behind MagdaApi so agents/scripting/CLI consumers don't depend on
 * the SelectionManager singleton directly. PR1 only exposes what
 * AutomationExecutor needs; subsequent PRs grow the surface as more
 * call sites migrate.
 */
class SelectionApi {
  public:
    virtual ~SelectionApi() = default;

    virtual TrackId getSelectedTrack() const = 0;

    virtual bool hasAutomationLaneSelection() const = 0;
    virtual const AutomationLaneSelection& getAutomationLaneSelection() const = 0;
};

}  // namespace magda
