// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "devices/faust/effects/Grit.hpp"
#include "plugins/compiled/CompiledEffectPlugin.hpp"

namespace magda::daw::audio::compiled {

using MagdaGritCompiledPlugin = CompiledEffectPlugin<devices::faust::Grit>;

}  // namespace magda::daw::audio::compiled
