// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace magda::daw::audio {

class InternalPluginRegistry;

/// Registers the devices shipped in MAGDA's base pack.
void registerBaseDevices(InternalPluginRegistry& registry);

}  // namespace magda::daw::audio
