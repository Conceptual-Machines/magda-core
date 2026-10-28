// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <magda/sdk/telemetry/Telemetry.hpp>
#include <string_view>

namespace magda::devices::spectral {

/**
 * @brief A spectral device's output spectrogram: a ring of columns of kColumnBands values.
 *
 * Each column is one STFT frame, mid-summed, in dBFS clamped at kFloorDb, on log-spaced bands
 * from kMinHz to kMaxHz, lowest first. writePosition() advances in whole columns.
 */
class SpectrogramTelemetry : public sdk::SampleRingTelemetry {
  public:
    static constexpr std::string_view kKey = "spectrogram";
    static constexpr int kColumnBands = 128;
    static constexpr float kMinHz = 20.0f;
    static constexpr float kMaxHz = 20000.0f;
    static constexpr float kFloorDb = -96.0f;
};

}  // namespace magda::devices::spectral
