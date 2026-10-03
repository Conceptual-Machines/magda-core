#pragma once

#include "devices/faust/effects/GrainDelay.hpp"
#include "plugins/compiled/CompiledEffectPlugin.hpp"

namespace magda::daw::audio::compiled {

using MagdaGrainDelayCompiledPlugin = CompiledEffectPlugin<devices::faust::GrainDelay>;

}  // namespace magda::daw::audio::compiled
