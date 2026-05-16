#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "../FaustParamPool.hpp"
#include "CompiledFaustInterface.hpp"
#include "core/ParameterInfo.hpp"

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
class MagdaFilterCompiledPlugin : public te::Plugin, public ICompiledFaustPlugin {
  public:
    static const char* xmlTypeName;

    explicit MagdaFilterCompiledPlugin(const te::PluginCreationInfo& info);
    ~MagdaFilterCompiledPlugin() override;

    juce::String getName() const override;
    juce::String getPluginType() override;
    juce::String getShortName(int) override;
    juce::String getSelectableDescription() override;

    void initialise(const te::PluginInitialisationInfo& info) override;
    void deinitialise() override;
    void reset() override;
    void applyToBuffer(const te::PluginRenderContext& fc) override;

    bool takesMidiInput() override {
        return false;
    }
    bool takesAudioInput() override {
        return true;
    }
    bool isSynth() override {
        return false;
    }
    bool producesAudioWhenNoAudioInput() override {
        return false;
    }
    double getTailLength() const override {
        return 0.0;
    }

    // Slot-indexed accessors for host params. Used by the curve view +
    // processor.
    static constexpr int kCutoffSlot = 0;
    static constexpr int kResonanceSlot = 1;
    static constexpr int kDriveSlot = 2;
    static constexpr int kEngineSlot = 3;
    static constexpr int kModeSlot = 4;
    static constexpr int kLimitSlot = 5;
    static constexpr int kHostSlotCount = 6;

    enum class FilterFamily { SVF, Ladder, Korg35, Oberheim, SallenKey, Diode };
    static constexpr int kEngineCount = 6;

    te::AutomatableParameter* getSlotParameter(int slotIndex) const;

    // Convert between display value (slider Hz / index) and the te native
    // (normalized 0..1) value for a given host slot. Mirrors the
    // MagdaFilterCompiledPlugin API so the curve view + processor work the
    // same way.
    float displayValueToNativeValue(int slotIndex, float displayValue) const;
    float nativeValueToDisplayValue(int slotIndex, float nativeValue) const;

    // The host slots, exposed as ParameterInfo so the processor can
    // populate device.parameters. Kind / range / scale baked here once at
    // construction.
    using HostSlotInfo = CompiledHostSlotInfo;
    const HostSlotInfo& getSlotInfo(int slotIndex) const;

    // Live engine index, derived from the host Engine param. Used by the
    // host glue (CompiledFaustProcessor / DeviceSlotComponent) to keep the
    // Mode slot's choice list in sync with the active engine's actual
    // mode set (e.g. Korg 35 = LP/HP, Sallen-Key = LP/BP/HP, Ladder = LP).
    int activeEngineIndex() const;
    std::vector<juce::String> modeChoicesForEngine(int engineIndex) const;

    // ICompiledFaustPlugin
    int hostSlotCount() const override {
        return kHostSlotCount;
    }
    const CompiledHostSlotInfo& hostSlotInfo(int slotIndex) const override {
        return getSlotInfo(slotIndex);
    }
    te::AutomatableParameter* hostSlotParameter(int slotIndex) const override {
        return getSlotParameter(slotIndex);
    }
    float displayToNormalized(int slotIndex, float displayValue) const override {
        return displayValueToNativeValue(slotIndex, displayValue);
    }
    float normalizedToDisplay(int slotIndex, float normalizedValue) const override {
        return nativeValueToDisplayValue(slotIndex, normalizedValue);
    }
    int engineAwareModeSlot() const override {
        return kModeSlot;
    }
    int activeEngine() const override {
        return activeEngineIndex();
    }
    std::vector<juce::String> modeChoicesForActiveEngine() const override {
        return modeChoicesForEngine(activeEngineIndex());
    }

  private:
    struct EngineDsp;

    void buildHostParameters();
    void rebuildEngineState(int sampleRate);
    void writeSharedZones();  // cutoff/res/drive into all engines
    void writeModeZone(int engineIndex);

    // Per-engine state — the dsp + the harvested binding for cutoff /
    // resonance / drive / mode (as float* pointers into the dsp). One
    // entry per FilterFamily.
    struct EngineState {
        std::unique_ptr<::dsp> dsp;
        FAUSTFLOAT* cutoffZone = nullptr;
        FAUSTFLOAT* resZone = nullptr;
        FAUSTFLOAT* driveZone = nullptr;
        FAUSTFLOAT* modeZone = nullptr;       // null if engine has no mode
        std::vector<float> modeChoiceValues;  // sorted underlying values for mode
        int sampleRate = 44100;
        int numInputs = 0;
        int numOutputs = 0;
    };
    std::array<EngineState, kEngineCount> engines_;
    std::atomic<int> activeEngine_{0};

    std::array<HostSlotInfo, kHostSlotCount> hostSlotInfo_;
    std::array<te::AutomatableParameter::Ptr, kHostSlotCount> hostParams_;
    std::array<juce::CachedValue<float>, kHostSlotCount> hostCached_;

    juce::AudioBuffer<float> scratchIn_;
    juce::AudioBuffer<float> scratchOut_;
    std::vector<float*> inPtrs_;
    std::vector<float*> outPtrs_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaFilterCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
