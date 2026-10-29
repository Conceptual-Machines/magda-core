// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include "core/ChainNodePath.hpp"

namespace magda::daw::ui {

/**
 * @brief Undoably removes the devices and racks at @p paths, as one step.
 *
 * Deferred, since the rebuild that follows destroys the component asking. Nodes inside a rack
 * that is also going are skipped.
 */
void deleteChainNodes(std::vector<magda::ChainNodePath> paths);

/** @brief The paths the Delete key and a node's Delete item act on: the selection when
 *  @p clicked is part of it, else @p clicked alone. */
std::vector<magda::ChainNodePath> chainNodesToDelete(const magda::ChainNodePath& clicked);

}  // namespace magda::daw::ui
