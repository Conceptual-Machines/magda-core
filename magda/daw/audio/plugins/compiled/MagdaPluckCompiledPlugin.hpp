#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "../FaustParamPool.hpp"
#include "CompiledFaustInterface.hpp"
#include "core/ParameterInfo.hpp"

// mydsp_poly (Faust's polyphonic voice allocator) is forward-declared via its
// base so the header doesn't pull in the Faust SDK; the .cpp owns it.
class dsp_poly;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust plucked-string instrument with a curve-shaped strum
 *        scheduler (the "Pluck" device — a port of the Retrospect spike).
 *
 * The voice is a pm.lib Karplus-Strong string (magda_pluck.dsp) wrapped at
 * runtime in mydsp_poly (group=false), exactly like MagdaPolySynth. What makes
 * this device distinct lives in C++: held MIDI notes are latched into a chord,
 * and a curve (placeholder cubic-Bezier shape presets x cycles) reshapes WHEN
 * each note of the chord is plucked. The scheduler keys voices on at
 * sample-accurate onsets, so a held chord becomes an expressive strum / roll /
 * arpeggio.
 *
 * Host slots are of two kinds:
 *  - Voice slots (Damping/Pluck Pos/Brightness/Drive) map to the Faust [idx:N]
 *    zones and are fanned out to every voice each block (as in MagdaPolySynth).
 *  - Scheduler slots (Trigger/Order/Shape/Cycles/Strum Length/Sync Interval)
 *    plus output Gain have NO Faust zone; their value is read in C++ to drive
 *    the scheduler and the output stage.
 */
class MagdaPluckCompiledPlugin : public te::Plugin, public ICompiledFaustPlugin {
  public:
    static const char* xmlTypeName;

    explicit MagdaPluckCompiledPlugin(const te::PluginCreationInfo& info);
    ~MagdaPluckCompiledPlugin() override;

    juce::String getName() const override;
    juce::String getPluginType() override;
    juce::String getShortName(int) override;
    juce::String getSelectableDescription() override;

    void initialise(const te::PluginInitialisationInfo& info) override;
    void deinitialise() override;
    void reset() override;
    void applyToBuffer(const te::PluginRenderContext& fc) override;

    bool takesMidiInput() override {
        return true;
    }
    bool takesAudioInput() override {
        return false;
    }
    bool isSynth() override {
        return true;
    }
    bool producesAudioWhenNoAudioInput() override {
        return true;
    }
    double getTailLength() const override {
        return 0.0;
    }

    // Voice slots (0..kVoiceSlotCount-1) map to magda_pluck.dsp's [idx:N] pins.
    static constexpr int kDampingSlot = 0;
    static constexpr int kPluckPosSlot = 1;
    static constexpr int kBrightnessSlot = 2;
    static constexpr int kDriveSlot = 3;
    static constexpr int kVoiceSlotCount = 4;

    // Scheduler / output slots — no Faust zone, read in C++.
    static constexpr int kTriggerSlot = 4;       // Chord / Sync
    static constexpr int kOrderSlot = 5;         // Up / Down / Up-Down / As Played
    static constexpr int kShapeSlot = 6;         // 8 curve presets
    static constexpr int kCyclesSlot = 7;        // 1..8 (tiled curve)
    static constexpr int kStrumLenSlot = 8;      // strum window, ms
    static constexpr int kSyncIntervalSlot = 9;  // re-strum interval (Sync), ms
    static constexpr int kGainSlot = 10;         // output gain, dB
    static constexpr int kHostSlotCount = 11;

    static constexpr int kNumVoices = 32;

    te::AutomatableParameter* getSlotParameter(int slotIndex) const;
    float displayValueToNativeValue(int slotIndex, float displayValue) const;
    float nativeValueToDisplayValue(int slotIndex, float nativeValue) const;

    using HostSlotInfo = CompiledHostSlotInfo;
    const HostSlotInfo& getSlotInfo(int slotIndex) const;

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

  private:
    void buildHostParameters();
    void rebuildEngineState(int sampleRate);
    magda::ParameterInfo infoForSlot(int slotIndex) const;
    float slotRealValue(int slotIndex) const;

    // --- Scheduler (ported from Retrospect PluckProcessor) ---
    struct Held {
        int note = 0;
        float vel = 0.0f;
        std::int64_t order = 0;
    };
    struct Pending {
        std::int64_t fireAt = 0;  // absolute sample clock
        int note = 0;
        int velocity = 0;    // 0..127
        bool gateOn = true;  // true = keyOn, false = keyOff
    };

    void handleMidi(const te::MidiMessageArray& midi);
    void scheduleStrum();
    void fireDuePlucks();
    void panic();

    std::unique_ptr<::dsp_poly> poly_;
    int numOutputs_ = 0;
    int currentSampleRate_ = 44100;

    // Per voice slot: that control's zone in EVERY voice (group=false).
    std::array<std::vector<FAUSTFLOAT*>, kVoiceSlotCount> voiceZonesBySlot_;

    std::array<HostSlotInfo, kHostSlotCount> hostSlotInfo_;
    std::array<te::AutomatableParameter::Ptr, kHostSlotCount> hostParams_;
    std::array<juce::CachedValue<float>, kHostSlotCount> hostCached_;

    std::vector<Held> held_;
    std::vector<Pending> pending_;
    std::int64_t clock_ = 0;         // absolute sample counter
    std::int64_t noteOrder_ = 0;     // play-order stamp for As-Played ordering
    int collectLeft_ = -1;           // Chord-mode collect debounce (samples)
    int syncLeft_ = 0;               // Sync-mode interval countdown (samples)
    int lutShape_ = -1;              // shape index the LUT was built for
    std::array<float, 1024> lut_{};  // current strum curve, sampled
    float limEnv_ = 0.0f;            // output limiter peak envelope

    juce::AudioBuffer<float> scratchOut_;
    std::vector<float*> outPtrs_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaPluckCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
