#pragma once

#include <vector>

#include "plugins/compiled/MagdaCompiledEffect.hpp"

class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Multi-engine compiled-Faust stereo widener.
 *
 * Engines:
 *  - Dimension: Roland Dimension D-style anti-phase modulated delays
 *    with cross-channel mixing. Subtle, breathing widening that lifts
 *    mono content into a wide stereo image. Backed by
 *    magda_dimension_dim.dsp.
 *  - Haas: short fixed delay on one channel — classic psychoacoustic
 *    cue. Mono-leaning but very wide. magda_dimension_haas.dsp.
 *  - M/S: pure mid-side side-channel gain — surgical stereo width
 *    without any time smear. magda_dimension_ms.dsp.
 *
 * Same Pattern B layout the Reverb wrapper uses: all three engine DSPs
 * are instantiated, only the active one's compute() runs per audio
 * callback. Shared zones (Amount / Rate / Width / Mix / Output) are
 * written into every engine every block so swapping engines preserves
 * the user's settings.
 */
class MagdaDimensionCompiledPlugin : public MagdaCompiledEffect {
  public:
    static const char* xmlTypeName;

    MagdaDimensionCompiledPlugin();

    static constexpr int kEngineSlot = 0;
    static constexpr int kAmountSlot = 1;
    static constexpr int kRateSlot = 2;
    static constexpr int kWidthSlot = 3;
    static constexpr int kMixSlot = 4;
    static constexpr int kOutputSlot = 5;
    static constexpr int kHostSlotCount = 6;
    enum class DimensionEngine { Dimension = 0, Haas = 1, MidSide = 2 };
    static constexpr int kEngineCount = 3;

    bool isSlotHiddenForActiveEngine(int slotIndex) const override {
        // Rate drives the Dimension engine's chorus modulator; the other two
        // have nothing to modulate, and Faust strips the zone accordingly.
        return slotIndex == kRateSlot &&
               activeEngine() != static_cast<int>(DimensionEngine::Dimension);
    }

    juce::String devicePluginId() const override {
        return xmlTypeName;
    }
    juce::String deviceName() const override {
        return "Dimension";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<HostSlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_dimension_";
    }
    int engineCount() const override {
        return kEngineCount;
    }
    int engineSlot() const override {
        return kEngineSlot;
    }
    int outputChannelCount() const override {
        return 2;
    }
    bool producesAudioWithoutInput() const override {
        return true;
    }
    double tailSeconds() const override {
        // Haas at 30 ms, times a safety margin.
        return 0.1;
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaDimensionCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
