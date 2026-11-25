// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>

#include "core/ChainNodePath.hpp"
#include "core/DeviceInfo.hpp"

namespace magda::daw::ui {

/** @brief The device a plugin browser drag describes, ready to add to a chain. */
magda::DeviceInfo deviceInfoFromPluginDrag(const juce::DynamicObject& obj);

/** @brief Whether a chain takes @p path when dropped: a sample or a chain, rack or device preset.
 */
bool isChainDroppableFile(const juce::String& path);

/**
 * @brief Drop @p files into @p chainPath at @p insertIndex, in order, and return how many elements
 *        went in. A sample becomes a new Sampler rooted on what its pad sends; a preset loads.
 */
int dropFilesIntoChain(const juce::StringArray& files, const magda::ChainNodePath& chainPath,
                       int insertIndex);

}  // namespace magda::daw::ui
