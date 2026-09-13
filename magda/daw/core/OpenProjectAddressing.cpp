#include "OpenProjectAddressing.hpp"

#include <variant>

#include "AutomationManager.hpp"
#include "TrackManager.hpp"
#include "controllers/BindingRegistry.hpp"

namespace magda {

std::vector<ControlTarget> boundControlTargets() {
    std::vector<ControlTarget> bound;
    auto& bindings = BindingRegistry::getInstance();

    for (const auto scope : {BindingScope::Global, BindingScope::Project})
        for (const auto& binding : bindings.bindings(scope))
            if (const auto* target = std::get_if<ControlTarget>(&binding.target))
                bound.push_back(*target);

    return bound;
}

AddressedParameters addressedInOpenProject() {
    auto& tracks = TrackManager::getInstance();
    const auto bound = boundControlTargets();

    AddressingSources sources;
    sources.tracks = tracks.getTracks();
    sources.master = tracks.getTrack(MASTER_TRACK_ID);
    sources.lanes = AutomationManager::getInstance().getLanes();
    sources.bound = bound;

    return AddressedParameters::from(sources);
}

}  // namespace magda
