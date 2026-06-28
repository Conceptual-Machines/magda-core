#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <atomic>
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
 * @brief Shared base for the compiled-Faust polyphonic instruments (FM, the
 *        drum-machine voices, ...).
 *
 * Holds everything identical across these devices: the mydsp_poly voice engine,
 * the per-voice [idx:N] macro fan-out, MIDI-driven voice allocation, and the
 * output gain + safety limiter.
 *
 * Host slots are laid out as [voice macros ...][Gain]:
 *  - Voice macros (0 .. voiceSlotCount-1) map 1:1 to the dsp's [idx:N] zones and
 *    are fanned out to every voice each block. A concrete device defines them in
 *    voiceSlotInfos().
 *  - Gain (the single control slot) has no dsp zone; it is applied in C++.
 *
 * A concrete device is a thin subclass: it supplies the voice dsp factory, the
 * voice-macro table, the id prefix and the name strings, then calls
 * initInstrument() from its constructor.
 *
 * Note: this is a plain instrument - chord strumming / arpeggiation lives in the
 * standalone Strum MIDI effect (MidiStrumPlugin), which can drive this or any
 * other instrument.
 */
class MagdaCompiledPolyInstrument : public te::Plugin, public ICompiledFaustPlugin {
  public:
    explicit MagdaCompiledPolyInstrument(const te::PluginCreationInfo& info);
    ~MagdaCompiledPolyInstrument() override;

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

    enum VoiceMode { Poly = 0, Mono = 1, Legato = 2 };

    int voiceSlotCount() const {
        return static_cast<int>(voiceSlotInfos_.size());
    }
    // Control slots follow the voice macros: Gain always, then Voice Mode when
    // the device supports mono/legato.
    int gainSlot() const {
        return voiceSlotCount();
    }
    int voiceModeSlot() const {
        return hasVoiceModes() ? voiceSlotCount() + 1 : -1;
    }
    int controlSlotCount() const {
        return hasVoiceModes() ? 2 : 1;
    }
    int hostSlotCountValue() const {
        return voiceSlotCount() + controlSlotCount();
    }

    te::AutomatableParameter* getSlotParameter(int slotIndex) const;
    float displayValueToNativeValue(int slotIndex, float displayValue) const;
    float nativeValueToDisplayValue(int slotIndex, float nativeValue) const;
    const HostSlotInfo& getSlotInfo(int slotIndex) const;

    // Monotonic note-on counter, bumped on every voice trigger. A device UI polls
    // it (on a timer) to flash a strike animation; RT-safe relaxed atomic.
    std::uint32_t strikePulse() const {
        return strikePulse_.load(std::memory_order_relaxed);
    }

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
    // Allocate one single-voice dsp instance (e.g. `new MagdaKickDsp()`). The
    // base wraps it in mydsp_poly.
    virtual ::dsp* createVoiceDsp() const = 0;
    // The voice-macro slots, in [idx:0..N-1] order. Their count defines where
    // the Gain control slot begins.
    virtual std::vector<HostSlotInfo> voiceSlotInfos() const = 0;
    // Parameter-id prefix, e.g. "magda_kick_". Must be stable (it keys state).
    virtual const char* slotIdPrefix() const = 0;
    // Voice-allocator size.
    virtual int numVoices() const {
        return 32;
    }
    // Override to add a Voice Mode (Poly/Mono/Legato) control slot, a dedicated
    // mono voice and glide. Default off (plain poly instrument).
    virtual bool hasVoiceModes() const {
        return false;
    }
    // The voice-macro slot carrying portamento Glide - forced to 0 on the poly
    // voices, driven on the mono voice. -1 if the dsp has no glide zone.
    virtual int glideVoiceSlot() const {
        return -1;
    }

    // Concrete constructors call this once, after their config hooks are valid.
    void initInstrument();

  private:
    void buildHostParameters();
    void rebuildEngineState(int sampleRate);
    magda::ParameterInfo infoForSlot(int slotIndex) const;
    float slotRealValue(int slotIndex) const;

    // ---- Voice-mode (Mono/Legato/glide) handling, active only when
    //      hasVoiceModes() is true ----------------------------------------
    int readVoiceModeIndex() const;
    void resetAllVoices();
    // Mono/Legato note handling on the dedicated single voice. Returns true when
    // a one-sample gate edge is needed to retrigger (Mono only).
    bool handleMonoNoteOn(int note, int velocity, int mode);
    void handleMonoNoteOff(int note);

    std::unique_ptr<::dsp_poly> poly_;
    // Dedicated single voice for Mono/Legato (the poly allocator skips idle
    // voices, so it cannot be driven zone-only). Allocated only with voice modes.
    std::unique_ptr<::dsp> monoVoice_;
    int numOutputs_ = 0;
    double sampleRate_ = 44100.0;

    // Mono voice's reserved freq/gain/gate zones + its copy of each voice-macro
    // zone (so glide etc. can be driven on it independently of the poly voices).
    FAUSTFLOAT* monoFreqZone_ = nullptr;
    FAUSTFLOAT* monoGainZone_ = nullptr;
    FAUSTFLOAT* monoGateZone_ = nullptr;
    std::vector<FAUSTFLOAT*> monoZonesBySlot_;
    struct HeldNote {
        int note = 0;
        float gain = 0.0f;
    };
    std::vector<HeldNote> heldNotes_;
    // Poly-mode sounding pitches. The Faust voice allocator hands out a fresh
    // voice on every keyOn without checking whether that pitch is already
    // sounding, so a duplicate note-on (or a dropped note-off) strands a voice
    // that never gets released. We release any existing voice for a pitch before
    // re-triggering it, guaranteeing one voice per pitch and no hung notes.
    std::array<bool, 128> polyHeld_{};
    int lastVoiceMode_ = 0;
    // Transport play state from the previous block. On the playing->stopped edge
    // we flush all voices: clip playback doesn't send note-offs when the user
    // hits Stop mid-note, so the Faust voice would stay gated on and keep
    // sounding (the graph runs continuously for live monitoring).
    bool wasPlaying_ = false;

    // Voice macros only (0 .. voiceSlotCount-1): that control's zone in EVERY
    // voice (group=false), so a single host value fans out to all voices.
    std::vector<std::vector<FAUSTFLOAT*>> voiceZonesBySlot_;

    std::vector<HostSlotInfo> voiceSlotInfos_;  // cached from the hook
    std::vector<HostSlotInfo> hostSlotInfo_;    // voice macros + Gain
    std::vector<te::AutomatableParameter::Ptr> hostParams_;
    // juce::CachedValue is non-movable, so a vector (which moves on growth) can't
    // hold it. deque is node-based: resize default-constructs in place without
    // ever moving existing elements, and it stays indexable.
    std::deque<juce::CachedValue<float>> hostCached_;

    float limEnv_ = 0.0f;  // output limiter peak envelope

    std::atomic<std::uint32_t> strikePulse_{0};  // note-on counter for UI strike flash

    juce::AudioBuffer<float> scratchOut_;
    std::vector<float*> outPtrs_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaCompiledPolyInstrument)
};

}  // namespace magda::daw::audio::compiled
