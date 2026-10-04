#pragma once

#include "devices/faust/effects/Delay.hpp"
#include "plugins/compiled/CompiledEffectPlugin.hpp"

namespace magda::daw::audio::compiled {

using MagdaDelayCompiledPlugin = CompiledEffectPlugin<devices::faust::Delay>;

}  // namespace magda::daw::audio::compiled
