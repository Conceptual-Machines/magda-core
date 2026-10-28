// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "devices/faust/effects/Compressor.hpp"
#include "plugins/compiled/CompiledEffectPlugin.hpp"

namespace magda::daw::audio::compiled {

using MagdaCompressorCompiledPlugin = CompiledEffectPlugin<devices::faust::Compressor>;

}  // namespace magda::daw::audio::compiled
