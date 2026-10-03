#pragma once

#include "devices/faust/effects/Dimension.hpp"
#include "plugins/compiled/CompiledEffectPlugin.hpp"

namespace magda::daw::audio::compiled {

using MagdaDimensionCompiledPlugin = CompiledEffectPlugin<devices::faust::Dimension>;

}  // namespace magda::daw::audio::compiled
