#pragma once

#include <atomic>
#include <vector>

#include "plugins/compiled/MagdaCompiledEffect.hpp"

class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Multi-engine compiled-Faust compressor.
 *
 * Engines:
 *  - Clean: hand-rolled FF compressor (peak/RMS detector, soft knee, stereo
 *    link, sidechain HPF, external audio sidechain, parallel mix, soft-limit
 *    output stage). Backed by magda_compressor.dsp.
 *  - Glue: Brouns FBFF compressor with user-exposed Peak/RMS detector,
 *    Pre/Post style, and FF↔FB blend; no sidechain HPF, no external
 *    sidechain. Backed by magda_compressor_glue.dsp.
 *
 * Both engine DSPs are instantiated; only the active one runs compute() per
 * audio callback. Shared zones (threshold/ratio/attack/release/etc.) are
 * written into both engines every block so swapping engines preserves the
 * user's settings. Engine-specific zones (SC HPF on Clean, FBFF/Style on
 * Glue) are written only when present on each engine.
 */
class MagdaCompressorCompiledPlugin : public MagdaCompiledEffect {
  public:
    static const char* xmlTypeName;

    MagdaCompressorCompiledPlugin();

    static constexpr int kEngineSlot = 0;
    static constexpr int kThresholdSlot = 1;
    static constexpr int kRatioSlot = 2;
    static constexpr int kAttackSlot = 3;
    static constexpr int kReleaseSlot = 4;
    static constexpr int kKneeSlot = 5;
    static constexpr int kMakeupSlot = 6;
    static constexpr int kMixSlot = 7;
    static constexpr int kOutputSlot = 8;
    static constexpr int kDetectorSlot = 9;
    static constexpr int kLinkSlot = 10;
    static constexpr int kSidechainHpfSlot = 11;  // Clean only
    static constexpr int kFbffSlot = 12;          // Glue only
    static constexpr int kStyleSlot = 13;         // Glue only — Pre / Post
    static constexpr int kAutogainSlot = 14;
    static constexpr int kHostSlotCount = 15;
    static constexpr int kUseSidechainHiddenSlot = 63;
    enum class CompressorEngine { Clean = 0, Glue = 1 };
    static constexpr int kEngineCount = 2;

    // Audio-thread metering taps for the transfer-curve view.
    float getInputPeakDb() const {
        return inputPeakDb_.load(std::memory_order_relaxed);
    }
    float getKeyPeakDb() const {
        return keyPeakDb_.load(std::memory_order_relaxed);
    }
    float getOutputPeakDb() const {
        return outputPeakDb_.load(std::memory_order_relaxed);
    }
    float getGainReductionDb() const {
        return gainReductionDb_.load(std::memory_order_relaxed);
    }
    bool isUsingExternalSidechain() const {
        return usingExternalSidechain_.load(std::memory_order_relaxed);
    }

    juce::String devicePluginId() const override {
        return xmlTypeName;
    }
    juce::String deviceName() const override {
        return "Compressor";
    }
    juce::String deviceShortName() const override {
        return "Comp";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<HostSlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_compressor_";
    }
    int engineCount() const override {
        return kEngineCount;
    }
    int engineSlot() const override {
        return kEngineSlot;
    }
    bool wantsSidechain() const override {
        return true;
    }
    int outputChannelCount() const override {
        return 2;
    }
    int inputChannelCount() const override {
        return 3;  // Left, Right, and the key.
    }
    void beforeCompute(DeviceProcessContext& context, int engineIndex) override;
    void afterCompute(DeviceProcessContext& context, int engineIndex) override;

  private:
    std::atomic<float> inputPeakDb_{-120.0f};
    std::atomic<float> keyPeakDb_{-120.0f};
    std::atomic<float> outputPeakDb_{-120.0f};
    std::atomic<float> gainReductionDb_{0.0f};
    std::atomic<bool> usingExternalSidechain_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaCompressorCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
