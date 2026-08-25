#pragma once

#include <vector>

#include "plugins/compiled/MagdaCompiledEffect.hpp"

class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Single compiled-Faust plugin hosting all six filter engine
 *        topologies (SVF, Moog ladder, Korg 35, Oberheim SEM, Sallen-Key,
 *        diode ladder).
 *
 * Holds six DSP instances simultaneously but only one runs `compute()`
 * per audio callback — the one selected by the Engine parameter. This
 * avoids Faust's "every selectn branch updates state" cost while keeping
 * a single device in MAGDA's picker.
 *
 * Engine switching is not seamless: the new engine's filter state is
 * stale (it hasn't been processing audio), so a switch produces a
 * one-shot click as state warms up. Acceptable trade for keeping all
 * six engines available without paying for them all every sample.
 *
 * Cutoff / Resonance / Drive map to the same idx across all six
 * engine DSPs, so the host writes one set of values into all six
 * zones every block — keeps every engine ready to take over on a
 * future Engine swap. Mode is engine-aware: SVF and Oberheim accept
 * all four (LP/BP/HP/Notch); Korg 35 LP+HP; Sallen-Key LP+BP+HP;
 * Ladder is LP-only. Unsupported modes fall back to LP for the engine.
 */
class MagdaFilterCompiledPlugin : public MagdaCompiledEffect {
  public:
    static const char* xmlTypeName;

    MagdaFilterCompiledPlugin();

    static constexpr int kCutoffSlot = 0;
    static constexpr int kResonanceSlot = 1;
    static constexpr int kDriveSlot = 2;
    static constexpr int kEngineSlot = 3;
    static constexpr int kModeSlot = 4;
    static constexpr int kLimitSlot = 5;
    static constexpr int kHostSlotCount = 6;
    enum class FilterFamily { SVF, Ladder, Korg35, Oberheim, SallenKey, Diode };
    static constexpr int kEngineCount = 6;

    /// The modes @p engineIndex can actually produce. Each family's Faust
    /// source declares only its own set, and the host's dropdown mirrors that
    /// so it never offers a mode with no branch behind it.
    std::vector<juce::String> modeChoicesForEngine(int engineIndex) const;

    int engineAwareModeSlot() const override {
        return kModeSlot;
    }
    std::vector<juce::String> modeChoicesForActiveEngine() const override {
        return modeChoicesForEngine(activeEngine());
    }

    juce::String devicePluginId() const override {
        return xmlTypeName;
    }
    juce::String deviceName() const override {
        return "Filter";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<HostSlotInfo> slotInfos() const override;
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
    void afterCompute(DeviceProcessContext& context, int engineIndex) override;

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaFilterCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
