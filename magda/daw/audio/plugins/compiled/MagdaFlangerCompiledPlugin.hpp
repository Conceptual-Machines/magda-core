#pragma once

#include "devices/faust/effects/Flanger.hpp"
#include "plugins/compiled/CompiledEffectPlugin.hpp"

namespace magda::daw::audio::compiled {

using MagdaFlangerCompiledPlugin = CompiledEffectPlugin<devices::faust::Flanger>;

}  // namespace magda::daw::audio::compiled
