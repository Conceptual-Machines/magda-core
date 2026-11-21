// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace magda::daw::ui::transport {

/**
 * @brief The slowest block's render time as a share of its deadline, held at its highest for a
 *        few readings so a single overrun stays on screen (#3375).
 */
class CpuDeadlineMark {
  public:
    static constexpr int kHoldReadings = 6;  // ~3 s at the meter's 0.5 s cadence
    static constexpr float kWarning = 0.7f;
    static constexpr float kMissed = 1.0f;

    void update(float worstBlock) {
        if (worstBlock >= held_ || ++readingsHeld_ >= kHoldReadings) {
            held_ = worstBlock;
            readingsHeld_ = 0;
        }
    }

    float value() const {
        return held_;
    }

  private:
    float held_ = 0.0f;
    int readingsHeld_ = 0;
};

}  // namespace magda::daw::ui::transport
