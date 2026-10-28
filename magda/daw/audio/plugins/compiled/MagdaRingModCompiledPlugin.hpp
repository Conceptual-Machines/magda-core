// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "devices/faust/effects/RingMod.hpp"
#include "plugins/compiled/CompiledEffectPlugin.hpp"

namespace magda::daw::audio::compiled {

using MagdaRingModCompiledPlugin = CompiledEffectPlugin<devices::faust::RingMod>;

}  // namespace magda::daw::audio::compiled
