#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "../../core/ControlTarget.hpp"

namespace magda {

namespace te = tracktion;

class PluginManager;
class TrackController;

class ControlTargetResolver {
  public:
    ControlTargetResolver(TrackController& trackController, PluginManager& pluginManager);

    te::AutomatableParameter* resolve(const ControlTarget& target) const;

  private:
    TrackController& trackController_;
    PluginManager& pluginManager_;
};

/**
 * @brief TE parameter value → MAGDA lane-normalized 0..1 for a target.
 *
 * Inverse of makeParameterValueConverter (AutomationBake.hpp) — keep the two
 * symmetric or the round-trip (MAGDA normalized -> TE raw -> MAGDA
 * normalized) drifts and the UI fights the curve. Shared by the playback
 * writeback path and AutomationManager's live current-value lookup.
 */
double laneNormalizedFromTEValue(const ControlTarget& target, te::AutomatableParameter* param,
                                 float teValue);

}  // namespace magda
