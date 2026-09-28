#pragma once

#include <vector>

#include "AddressedParameters.hpp"
#include "ControlTarget.hpp"

namespace magda {

/// Every controller binding and MIDI learn, as the addresses they name. Message thread.
std::vector<ControlTarget> boundControlTargets();

/// What the open project addresses: its tracks, lanes and bindings. Message thread.
AddressedParameters addressedInOpenProject();

}  // namespace magda
