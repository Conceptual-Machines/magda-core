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

}  // namespace magda
