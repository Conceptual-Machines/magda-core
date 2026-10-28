// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "devices/faust/effects/GateExpander.hpp"
#include "plugins/compiled/CompiledEffectPlugin.hpp"

namespace magda::daw::audio::compiled {

using MagdaGateExpanderCompiledPlugin = CompiledEffectPlugin<devices::faust::GateExpander>;

}  // namespace magda::daw::audio::compiled
