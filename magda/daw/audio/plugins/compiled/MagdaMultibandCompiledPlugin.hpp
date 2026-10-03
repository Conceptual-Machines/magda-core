#pragma once

#include "devices/faust/effects/Multiband.hpp"
#include "plugins/compiled/CompiledEffectPlugin.hpp"

namespace magda::daw::audio::compiled {

using MagdaMultibandCompiledPlugin = CompiledEffectPlugin<devices::faust::Multiband>;

}  // namespace magda::daw::audio::compiled
