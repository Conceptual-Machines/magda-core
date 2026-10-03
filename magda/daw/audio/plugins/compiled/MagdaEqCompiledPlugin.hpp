#pragma once

#include "devices/faust/effects/Eq.hpp"
#include "plugins/compiled/CompiledEffectPlugin.hpp"

namespace magda::daw::audio::compiled {

using MagdaEqCompiledPlugin = CompiledEffectPlugin<devices::faust::Eq>;

}  // namespace magda::daw::audio::compiled
