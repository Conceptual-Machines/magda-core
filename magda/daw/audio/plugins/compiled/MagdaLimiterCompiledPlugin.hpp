#pragma once

#include "devices/faust/effects/Limiter.hpp"
#include "plugins/compiled/CompiledEffectPlugin.hpp"

namespace magda::daw::audio::compiled {

using MagdaLimiterCompiledPlugin = CompiledEffectPlugin<devices::faust::Limiter>;

}  // namespace magda::daw::audio::compiled
