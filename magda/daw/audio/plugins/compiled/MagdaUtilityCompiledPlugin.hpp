// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "devices/faust/effects/Utility.hpp"
#include "plugins/compiled/CompiledEffectPlugin.hpp"

namespace magda::daw::audio::compiled {

using MagdaUtilityCompiledPlugin = CompiledEffectPlugin<devices::faust::Utility>;

}  // namespace magda::daw::audio::compiled
