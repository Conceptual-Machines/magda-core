#pragma once

#include "core/ChainNodePath.hpp"

namespace magda::daw::ui {

inline magda::ChainNodePath nearestRackPathForDevicePath(const magda::ChainNodePath& devicePath) {
    magda::ChainNodePath rackPath;
    rackPath.trackId = devicePath.trackId;
    int rackStepIndex = -1;
    for (int i = 0; i < static_cast<int>(devicePath.steps.size()); ++i) {
        if (devicePath.steps[static_cast<size_t>(i)].type == magda::ChainStepType::Rack)
            rackStepIndex = i;
    }
    if (rackStepIndex >= 0) {
        rackPath.steps.assign(devicePath.steps.begin(),
                              devicePath.steps.begin() + rackStepIndex + 1);
    }
    return rackPath;
}

inline magda::ChainNodePath modulationOwnerPathForSelection(
    const magda::ChainNodePath& selectionPath) {
    return selectionPath.getType() == magda::ChainNodeType::Track
               ? magda::ChainNodePath::trackLevel(selectionPath.trackId)
               : selectionPath;
}

}  // namespace magda::daw::ui
