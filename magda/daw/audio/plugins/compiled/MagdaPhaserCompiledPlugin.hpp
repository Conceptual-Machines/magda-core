// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "devices/faust/effects/Phaser.hpp"
#include "plugins/compiled/CompiledEffectPlugin.hpp"

namespace magda::daw::audio::compiled {

using MagdaPhaserCompiledPlugin = CompiledEffectPlugin<devices::faust::Phaser>;

}  // namespace magda::daw::audio::compiled
