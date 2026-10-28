// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "core/DeviceInfo.hpp"

/**
 * @file AraSupport.hpp
 * @brief Whether a plugin offers ARA, as its scan recorded it (#1579).
 *
 * The scan fills PluginDescription::hasARAExtension only in a build with ARA hosting on, so a
 * list scanned before that answers false until the plugin is scanned again.
 */

namespace magda::daw::audio::ara {

/** @brief Whether the installed plugin @p device resolves to in @p known offers ARA. */
bool supportsAra(const DeviceInfo& device, const juce::KnownPluginList& known);

}  // namespace magda::daw::audio::ara
