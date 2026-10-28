// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "devices/faust/effects/FreqShift.hpp"
#include "plugins/compiled/CompiledEffectPlugin.hpp"

namespace magda::daw::audio::compiled {

using MagdaFreqShiftCompiledPlugin = CompiledEffectPlugin<devices::faust::FreqShift>;

}  // namespace magda::daw::audio::compiled
