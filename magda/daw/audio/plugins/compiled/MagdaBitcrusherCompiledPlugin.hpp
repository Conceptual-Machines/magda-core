// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "devices/faust/effects/Bitcrusher.hpp"
#include "plugins/compiled/CompiledEffectPlugin.hpp"

namespace magda::daw::audio::compiled {

using MagdaBitcrusherCompiledPlugin = CompiledEffectPlugin<devices::faust::Bitcrusher>;

}  // namespace magda::daw::audio::compiled
