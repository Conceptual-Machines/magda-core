#pragma once

#include "devices/faust/effects/Pitch.hpp"
#include "plugins/compiled/CompiledEffectPlugin.hpp"

namespace magda::daw::audio::compiled {

using MagdaPitchCompiledPlugin = CompiledEffectPlugin<devices::faust::Pitch>;

}  // namespace magda::daw::audio::compiled
