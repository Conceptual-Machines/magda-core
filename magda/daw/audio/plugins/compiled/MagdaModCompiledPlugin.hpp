#pragma once

#include "devices/faust/effects/Mod.hpp"
#include "plugins/compiled/CompiledEffectPlugin.hpp"

namespace magda::daw::audio::compiled {

using MagdaModCompiledPlugin = CompiledEffectPlugin<devices::faust::Mod>;

}  // namespace magda::daw::audio::compiled
