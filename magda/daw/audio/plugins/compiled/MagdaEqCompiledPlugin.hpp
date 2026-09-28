#pragma once

#include <array>
#include <atomic>
#include <vector>

#include "analysis/AudioTapBuffer.hpp"
#include "plugins/compiled/MagdaCompiledEffect.hpp"

namespace magda::daw::audio::compiled {

/**
 * @brief Built-in 8-band parametric EQ.
 *
 * Each band carries Enabled plus its own filter Type (HP / LowShelf / Bell /
 * HighShelf / LP / Notch), Freq / Gain / Q. Audio runs through MAGDA-owned RBJ
 * biquads so the audible response and curve view share coefficient math.
 *
 * Slot layout (41 slots):
 *   5*band + 0 → Enabled (boolean)
 *   5*band + 1 → Type    (discrete menu, 0..5)
 *   5*band + 2 → Freq    (Hz, log)
 *   5*band + 3 → Gain    (dB, ±24)
 *   5*band + 4 → Q       (0.1..10)
 *   40         → Output  (dB, -24..+12)
 */
class MagdaEqCompiledPlugin : public MagdaCompiledEffect {
  public:
    static const char* xmlTypeName;

    MagdaEqCompiledPlugin();

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
        Notch = 5
    };
    static constexpr int kBandTypeCount = 6;
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
    const magda::daw::audio::AudioTapBuffer& getPreSpectrumTapBuffer() const {
        return preSpectrumTap_;
    }
    const magda::daw::audio::AudioTapBuffer& getPostSpectrumTapBuffer() const {
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
    void setCurveCollapsed(bool collapsed) {
        curveCollapsed_ = collapsed;
    }

    void flushState(juce::ValueTree& state) override;
    void restoreState(const juce::ValueTree& state) override;

    struct BiquadState {
        float x1 = 0.0f;
        float x2 = 0.0f;
        float y1 = 0.0f;
        float y2 = 0.0f;
    };

    static int bandSlot(int band, int offset) {
        return band * kSlotsPerBand + offset;
    }

    juce::String devicePluginId() const override {
        return xmlTypeName;
    }
    juce::String deviceName() const override {
        return "EQ";
    }

  protected:
    std::vector<HostSlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_eq_";
    }
    juce::String slotId(int slotIndex) const override;
    void onPrepare(double sampleRate, int maximumBlockSize) override;
    void onRelease() override;
    void onReset() override;
    void processAudio(DeviceProcessContext& context) override;

  private:
    bool curveCollapsed_ = true;

    std::vector<float> preTapScratch_;
    std::vector<float> postTapScratch_;
    magda::daw::audio::AudioTapBuffer preSpectrumTap_{8192};
    magda::daw::audio::AudioTapBuffer postSpectrumTap_{8192};
    std::array<std::vector<BiquadState>, kBandCount> biquadStates_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaEqCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
