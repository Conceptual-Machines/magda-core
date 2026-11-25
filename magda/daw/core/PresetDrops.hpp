// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file PresetDrops.hpp
 * @brief What a preset dropped on a chain does to it (#1329, #3416).
 */

#pragma once

#include <optional>

#include "ChainNodePath.hpp"
#include "PresetManager.hpp"
#include "RackInfo.hpp"

namespace magda {

/**
 * @brief Inserts @p rack, loaded from a rack preset, into the chain at @p chainPath.
 *
 * One undo step, with fresh ids throughout, so the same preset can be dropped twice.
 * @return false when nothing was inserted.
 */
bool insertRackPreset(const ChainNodePath& chainPath, RackInfo rack, int insertIndex);

/** @brief The preset in @p file when it is one a chain takes: a chain, rack or device preset. */
std::optional<PresetManager::PresetRef> chainDropPreset(const juce::File& file);

/** @brief What a preset dropped on a chain did. */
struct PresetDropResult {
    bool applied = false;
    int elementsInserted = 0;  ///< How far the next drop at the same place moves along
    juce::String errorTitle;   ///< Empty when there is nothing to report
    juce::String error;
};

/**
 * @brief Loads @p ref into the chain at @p chainPath: a track's own list, a rack chain or a pad's.
 *
 * A chain preset replaces a track's chain and joins any other chain at @p insertIndex; a rack or
 * device preset goes in at @p insertIndex. Edits the model directly, with no undo step.
 */
PresetDropResult loadPresetIntoChain(const PresetManager::PresetRef& ref,
                                     const ChainNodePath& chainPath, int insertIndex);

/**
 * @brief loadPresetIntoChain() with the undo the chain has: one pad edit on a pad's chain, the
 *        rack's insert for a rack preset anywhere else.
 */
PresetDropResult dropPresetIntoChain(const PresetManager::PresetRef& ref,
                                     const ChainNodePath& chainPath, int insertIndex);

}  // namespace magda
