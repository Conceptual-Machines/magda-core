#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include "../FaustParamPool.hpp"
#include "CompiledFaustInterface.hpp"
#include "core/ParameterInfo.hpp"

// mydsp_poly (Faust's polyphonic voice allocator) and the single-voice dsp are
// forward-declared via their Faust base so the header doesn't pull in the Faust
// SDK; the .cpp owns them.
class dsp;
class dsp_poly;

namespace magda::daw::audio::compiled {

/**
 * @brief Shared base for the curve-strummed compiled-Faust instruments (Pluck,
 *        Mallet, ...).
 *
 * Everything that is identical across these devices lives here: the mydsp_poly
 * voice engine, the per-voice [idx:N] macro fan-out, the chord-latch + curve
 * strum scheduler (a held chord is strummed / arpeggiated in time by a curve),
 * the output gain + peak limiter, and all the host-parameter plumbing.
 *
 * Host slots are laid out as [voice macros ...][control slots ...]:
 *  - Voice macros (0 .. voiceSlotCount-1) map 1:1 to the dsp's [idx:N] zones and
 *    are fanned out to every voice each block. A concrete device defines them in
 *    voiceSlotInfos().
 *  - Control slots (Trigger / Order / Shape / Cycles / Strum Length /
 *    Sync Interval / Gain) have NO dsp zone; their values are read in C++ to
 *    drive the scheduler and output stage. They are identical for every device.
 *
 * A concrete device is a thin subclass: it supplies the voice dsp factory, the
 * voice-macro table, the id prefix and the name strings, then calls
 * initInstrument() from its constructor.
 */
class MagdaStrumInstrument : public te::Plugin, public ICompiledFaustPlugin {
  public:
    explicit MagdaStrumInstrument(const te::PluginCreationInfo& info);
    ~MagdaStrumInstrument() override;

    // te::Plugin (shared across the family).
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

    using HostSlotInfo = CompiledHostSlotInfo;

    // Control slots are appended after the voice macros, in this order.
    enum ControlSlot {
        kTrigger = 0,       // Chord / Sync
        kOrder,             // Up / Down / Up-Down / As Played
        kShape,             // 8 curve presets
        kCycles,            // 1..8 (tiled curve)
        kStrumLen,          // strum window, ms
        kSyncInterval,      // re-strum interval (Sync), ms
        kGain,              // output gain, dB
        kControlSlotCount,  // == number of control slots
    };

    int voiceSlotCount() const {
        return static_cast<int>(voiceSlotInfos_.size());
    }
    int controlBaseSlot() const {
        return voiceSlotCount();
    }
    int controlSlot(ControlSlot c) const {
        return controlBaseSlot() + static_cast<int>(c);
    }
    int hostSlotCountValue() const {
        return voiceSlotCount() + kControlSlotCount;
    }

    te::AutomatableParameter* getSlotParameter(int slotIndex) const;
    float displayValueToNativeValue(int slotIndex, float displayValue) const;
    float nativeValueToDisplayValue(int slotIndex, float nativeValue) const;
    const HostSlotInfo& getSlotInfo(int slotIndex) const;

    // ICompiledFaustPlugin
    int hostSlotCount() const override {
        return hostSlotCountValue();
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

  protected:
    // ---- Hooks a concrete device implements -------------------------------
    // Allocate one single-voice dsp instance (e.g. `new MagdaPluckDsp()`). The
    // base wraps it in mydsp_poly.
    virtual ::dsp* createVoiceDsp() const = 0;
    // The voice-macro slots, in [idx:0..N-1] order. Their count defines where
    // the control slots begin.
    virtual std::vector<HostSlotInfo> voiceSlotInfos() const = 0;
    // Parameter-id prefix, e.g. "magda_pluck_". Must be stable (it keys state).
    virtual const char* slotIdPrefix() const = 0;
    // Voice-allocator size. Percussion can run leaner than 32.
    virtual int numVoices() const {
        return 32;
    }

    // Concrete constructors call this once, after their config hooks are valid.
    void initInstrument();

  private:
    void buildHostParameters();
    void rebuildEngineState(int sampleRate);
    magda::ParameterInfo infoForSlot(int slotIndex) const;
    float slotRealValue(int slotIndex) const;

    // ---- Scheduler (ported from the Pluck device) -------------------------
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

    // Voice macros only (0 .. voiceSlotCount-1): that control's zone in EVERY
    // voice (group=false), so a single host value fans out to all voices.
    std::vector<std::vector<FAUSTFLOAT*>> voiceZonesBySlot_;

    std::vector<HostSlotInfo> voiceSlotInfos_;  // cached from the hook
    std::vector<HostSlotInfo> hostSlotInfo_;    // voice macros + control slots
    std::vector<te::AutomatableParameter::Ptr> hostParams_;
    // juce::CachedValue is non-movable, so a vector (which moves on growth) can't
    // hold it. deque is node-based: resize default-constructs in place without
    // ever moving existing elements, and it stays indexable.
    std::deque<juce::CachedValue<float>> hostCached_;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaStrumInstrument)
};

}  // namespace magda::daw::audio::compiled
