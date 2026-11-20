// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace magda {

/**
 * @brief MAGDA's share of the machine's CPU between two reads, as the OS counts it across every
 *        core: what Activity Monitor or Task Manager shows, divided by the core count.
 */
class ProcessCpuMeter {
  public:
    /// The share since the last read, 0 to 1. Too soon after it, the last share again.
    double sample();

  private:
    double lastCpuSeconds_ = -1.0;
    double lastWallSeconds_ = 0.0;
    double share_ = 0.0;
};

}  // namespace magda
