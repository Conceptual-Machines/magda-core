#pragma once

#include "devices/faust/effects/Chorus.hpp"
#include "plugins/compiled/CompiledEffectPlugin.hpp"

namespace magda::daw::audio::compiled {

using MagdaChorusCompiledPlugin = CompiledEffectPlugin<devices::faust::Chorus>;

}  // namespace magda::daw::audio::compiled
