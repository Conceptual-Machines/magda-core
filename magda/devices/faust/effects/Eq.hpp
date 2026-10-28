// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <magda/sdk/tap/SampleRing.hpp>
#include <vector>

#include "devices/faust/CompiledEffect.hpp"

namespace magda::devices::faust {

/**
 * @brief Built-in 8-band parametric EQ.
 *
 * Each band carries Enabled plus its own filter Type (HP / LowShelf / Bell /
 * HighShelf / LP / Notch, and 24 dB/oct HP / LP), Freq / Gain / Q. Audio runs through MAGDA-owned
 * RBJ biquads so the audible response and curve view share coefficient math.
 *
 * Slot layout (41 slots):
 *   5*band + 0 → Enabled (boolean)
 *   5*band + 1 → Type    (discrete menu, 0..7)
 *   5*band + 2 → Freq    (Hz, log)
 *   5*band + 3 → Gain    (dB, ±24)
 *   5*band + 4 → Q       (0.1..10)
 *   40         → Output  (dB, -24..+12)
 */
class Eq : public CompiledEffect {
  public:
    static constexpr const char* xmlTypeName = "magda_eq";

    Eq();

    static constexpr int kBandCount = 8;
    static constexpr int kSlotsPerBand = 5;                         // Enabled, Type, Freq, Gain, Q
    static constexpr int kOutputSlot = kBandCount * kSlotsPerBand;  // 40
    static constexpr int kHostSlotCount = kOutputSlot + 1;          // 41
    enum class BandType {
        Highpass = 0,
        LowShelf = 1,
        Bell = 2,
        HighShelf = 3,
        Lowpass = 4,
        Notch = 5,
        Highpass24 = 6,  ///< Appended, so saved ordinals keep their meaning.
        Lowpass24 = 7
    };
    static constexpr int kBandTypeCount = 8;
    static constexpr int kBandEnabledOffset = 0;
    static constexpr int kBandTypeOffset = 1;
    static constexpr int kBandFreqOffset = 2;
    static constexpr int kBandGainOffset = 3;
    static constexpr int kBandQOffset = 4;

    /// Live per-band state: the values currently driving the audio thread.
    struct BandSnapshot {
        bool enabled = false;
        BandType type = BandType::Bell;
        float freq = 1000.0f;
        float gainDb = 0.0f;
        float q = 1.0f;
    };
    BandSnapshot getBandSnapshot(int band) const;
    float getOutputDb() const {
        return slotDisplayValue(kOutputSlot);
    }
    const engine::SampleRing& getPreSpectrumTapBuffer() const {
        return preSpectrumTap_;
    }
    const engine::SampleRing& getPostSpectrumTapBuffer() const {
        return postSpectrumTap_;
    }
    double getSampleRate() const {
        return currentSampleRate();
    }

    /// "Collapse knobs" toggle, persisted on the device's state so the user's
    /// preferred slot layout survives a project reload. Defaults to true: the
    /// curve is the EQ's primary surface.
    bool isCurveCollapsed() const {
        return curveCollapsed_;
    }
    /// Reported to the host, which writes it into the document.
    void setCurveCollapsed(bool collapsed);

    sdk::RestoreResult restoreState(const sdk::StateNode& state) override;

    static constexpr const char* kCurveCollapsedKey = "curveCollapsed";

    /** @brief One RBJ biquad, normalised so a0 is 1. */
    struct Coeffs {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
    };

    /** @brief The cascade a band runs: one biquad, or two for the 24 dB/oct types. */
    struct Stages {
        std::array<Coeffs, 2> coeffs{};
        int count = 1;
    };

    /// The audio path and the curve view both design from this.
    static Stages designBand(BandType type, float freq, float gainDb, float q, float sampleRate);

    struct BiquadState {
        float x1 = 0.0f;
        float x2 = 0.0f;
        float y1 = 0.0f;
        float y2 = 0.0f;
    };

    static int bandSlot(int band, int offset) {
        return band * kSlotsPerBand + offset;
    }

    std::string devicePluginId() const override {
        return xmlTypeName;
    }
    std::string deviceName() const override {
        return "EQ";
    }

  protected:
    std::vector<SlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_eq_";
    }
    std::string slotId(int slotIndex) const override;
    void onPrepare(double sampleRate, int maximumBlockSize) override;
    void onRelease() override;
    void onReset() override;
    void processAudio(sdk::ProcessContext& context) override;

  private:
    bool curveCollapsed_ = true;

    engine::SampleRing preSpectrumTap_{8192};
    engine::SampleRing postSpectrumTap_{8192};
    /// Per band, two stages per channel: channel * 2 + stage.
    std::array<std::vector<BiquadState>, kBandCount> biquadStates_;
};

}  // namespace magda::devices::faust
