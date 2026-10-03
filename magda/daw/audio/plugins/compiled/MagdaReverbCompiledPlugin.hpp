#pragma once

#include "devices/faust/effects/Reverb.hpp"
#include "plugins/compiled/CompiledEffectPlugin.hpp"

namespace magda::daw::audio::compiled {

using MagdaReverbCompiledPlugin = CompiledEffectPlugin<devices::faust::Reverb>;

}  // namespace magda::daw::audio::compiled
