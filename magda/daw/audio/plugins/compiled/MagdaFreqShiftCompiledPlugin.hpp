#pragma once

#include "devices/faust/effects/FreqShift.hpp"
#include "plugins/compiled/CompiledEffectPlugin.hpp"

namespace magda::daw::audio::compiled {

using MagdaFreqShiftCompiledPlugin = CompiledEffectPlugin<devices::faust::FreqShift>;

}  // namespace magda::daw::audio::compiled
