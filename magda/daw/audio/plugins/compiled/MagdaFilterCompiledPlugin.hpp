#pragma once

#include "devices/faust/effects/Filter.hpp"
#include "plugins/compiled/CompiledEffectPlugin.hpp"

namespace magda::daw::audio::compiled {

using MagdaFilterCompiledPlugin = CompiledEffectPlugin<devices::faust::Filter>;

}  // namespace magda::daw::audio::compiled
