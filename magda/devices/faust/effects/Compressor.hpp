#pragma once

#include <atomic>
#include <vector>

#include "devices/faust/CompiledEffect.hpp"

namespace magda::devices::faust {

/**
 * @brief Two-engine compiled-Faust compressor: Clean (feed-forward, sidechain HPF, external key)
 *        and Glue (Brouns FBFF, no external key).
 *
 * Shared zones are written into both engines every block, so switching keeps the settings.
 */
class Compressor : public CompiledEffect {
  public:
    static constexpr const char* xmlTypeName = "magda_compressor";

    Compressor();

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
    static constexpr int kStyleSlot = 13;         // Glue only: Pre / Post
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

    std::string devicePluginId() const override {
        return xmlTypeName;
    }
    std::string deviceName() const override {
        return "Compressor";
    }
    std::string deviceShortName() const override {
        return "Comp";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<SlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_compressor_";
    }
    int engineCount() const override {
        return kEngineCount;
    }
    int engineSlot() const override {
        return kEngineSlot;
    }
    sdk::SidechainPort sidechainPort() const override {
        return sdk::monoAudioSidechain;
    }
    int outputChannelCount() const override {
        return 2;
    }
    int inputChannelCount() const override {
        return 2;  // Left and Right. The key is a port, not a third input.
    }
    void beforeCompute(sdk::ProcessContext& context, int engineIndex) override;
    void afterCompute(sdk::ProcessContext& context, int engineIndex) override;

  private:
    std::atomic<float> inputPeakDb_{-120.0f};
    std::atomic<float> keyPeakDb_{-120.0f};
    std::atomic<float> outputPeakDb_{-120.0f};
    std::atomic<float> gainReductionDb_{0.0f};
    std::atomic<bool> usingExternalSidechain_{false};
};

}  // namespace magda::devices::faust
