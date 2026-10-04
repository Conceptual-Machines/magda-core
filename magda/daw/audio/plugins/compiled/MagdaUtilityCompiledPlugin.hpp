#pragma once

#include "devices/faust/effects/Utility.hpp"
#include "plugins/compiled/CompiledEffectPlugin.hpp"

namespace magda::daw::audio::compiled {

using MagdaUtilityCompiledPlugin = CompiledEffectPlugin<devices::faust::Utility>;

}  // namespace magda::daw::audio::compiled
