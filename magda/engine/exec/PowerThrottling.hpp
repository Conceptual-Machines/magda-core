// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file PowerThrottling.hpp
 * @brief Keeps Windows' EcoQoS off the audio threads while MAGDA is in the background (#3403).
 *
 * MAGDA_POWER_THROTTLING=allow leaves both to Windows, for measuring what the exemption buys.
 * Off Windows every call does nothing.
 */

namespace magda::engine::power_throttling {

/** @brief Opts the calling thread out of execution-speed throttling. */
void exemptCurrentThread();

/** @brief Opts the process out while @p exempt, and hands it back to Windows otherwise. */
void exemptProcess(bool exempt);

}  // namespace magda::engine::power_throttling
