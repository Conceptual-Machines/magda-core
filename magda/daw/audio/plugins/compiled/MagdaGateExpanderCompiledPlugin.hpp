#pragma once

#include "devices/faust/effects/GateExpander.hpp"
#include "plugins/compiled/CompiledEffectPlugin.hpp"

namespace magda::daw::audio::compiled {

using MagdaGateExpanderCompiledPlugin = CompiledEffectPlugin<devices::faust::GateExpander>;

}  // namespace magda::daw::audio::compiled
