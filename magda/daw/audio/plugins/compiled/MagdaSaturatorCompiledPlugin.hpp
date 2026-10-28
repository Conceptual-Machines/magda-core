// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "devices/faust/effects/Saturator.hpp"
#include "plugins/compiled/CompiledEffectPlugin.hpp"

namespace magda::daw::audio::compiled {

using MagdaSaturatorCompiledPlugin = CompiledEffectPlugin<devices::faust::Saturator>;

}  // namespace magda::daw::audio::compiled
