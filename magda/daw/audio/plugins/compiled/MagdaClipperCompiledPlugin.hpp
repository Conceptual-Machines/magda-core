#pragma once

#include "devices/faust/effects/Clipper.hpp"
#include "plugins/compiled/CompiledEffectPlugin.hpp"

namespace magda::daw::audio::compiled {

using MagdaClipperCompiledPlugin = CompiledEffectPlugin<devices::faust::Clipper>;

}  // namespace magda::daw::audio::compiled
