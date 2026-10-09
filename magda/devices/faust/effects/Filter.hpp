#pragma once

#include <magda/sdk/tap/SampleRing.hpp>
#include <string>
#include <vector>

#include "devices/faust/CompiledEffect.hpp"

namespace magda::devices::faust {

/**
 * @brief One device over six filter engines (SVF, Moog ladder, Korg 35, Oberheim SEM, Sallen-Key,
 *        diode ladder); only the one Engine selects runs compute().
 *
 * Every engine gets the shared zones each block so a switch finds its settings, though its filter
 * state is stale and clicks once. Mode is engine-aware; an unsupported mode falls back to LP.
 */
class Filter : public CompiledEffect {
  public:
    static constexpr const char* xmlTypeName = "magda_filter";

    Filter();

    static constexpr int kCutoffSlot = 0;
    static constexpr int kResonanceSlot = 1;
    static constexpr int kDriveSlot = 2;
    static constexpr int kEngineSlot = 3;
    static constexpr int kModeSlot = 4;
    static constexpr int kLimitSlot = 5;
    static constexpr int kHostSlotCount = 6;
    enum class FilterFamily { SVF, Ladder, Korg35, Oberheim, SallenKey, Diode };
    static constexpr int kEngineCount = 6;

    /// The modes @p engineIndex's dsp declares, so the dropdown never offers one it cannot produce.
    static std::vector<std::string> modeChoicesForEngine(int engineIndex);

    int engineAwareModeSlot() const override {
        return kModeSlot;
    }
    std::vector<std::string> engineModeChoices() const override {
        return modeChoicesForEngine(activeEngine());
    }

    /// The signal in and out, mixed to mono, for the faceplate's spectrum.
    const engine::SampleRing& getPreSpectrumTapBuffer() const {
        return preSpectrumTap_;
    }
    const engine::SampleRing& getPostSpectrumTapBuffer() const {
        return postSpectrumTap_;
    }
    double getSampleRate() const {
        return currentSampleRate();
    }

    std::string devicePluginId() const override {
        return xmlTypeName;
    }
    std::string deviceName() const override {
        return "Filter";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<SlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_filter_";
    }
    int engineCount() const override {
        return kEngineCount;
    }
    int engineSlot() const override {
        return kEngineSlot;
    }
    int slotForDspIdx(int idx) const override;
    void writeExtraZones(int engineIndex) override;
    void beforeCompute(sdk::ProcessContext& context, int engineIndex) override;
    void afterCompute(sdk::ProcessContext& context, int engineIndex) override;

  private:
    engine::SampleRing preSpectrumTap_{8192};
    engine::SampleRing postSpectrumTap_{8192};
};

}  // namespace magda::devices::faust
