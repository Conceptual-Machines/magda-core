#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "FaustParamPool.hpp"
#include "IFaustEditorModel.hpp"

// libfaust types are forward-declared so consumers don't need the Faust
// runtime headers on their include path. Implementation pulls them in.
class dsp;
class dsp_factory;
class dsp_poly;

namespace magda::daw::audio {

namespace te = tracktion::engine;

// A Faust-based polyphonic *instrument*. Sibling of FaustPlugin (the
// effect host) — it shares the same runtime-compile + FaustParamPool design,
// but reports as a synth, wraps the compiled DSP in a polyphonic voice
// allocator (mydsp_poly), and drives note allocation from the MIDI in the
// PluginRenderContext. The .dsp source is expected to follow the Faust
// polyphonic convention: the reserved controls `freq`, `gain`, `gate` are
// driven per-voice by the allocator and are NOT exposed as user parameters.
//
// Shared scaffolding (UIHarvester, pool rebind, atomic state swap, retire
// timer) is still copied from FaustPlugin rather than shared; a future
// refactor can extract a common base. See docs/FAUST_POOL_REFACTOR.md.
class FaustInstrumentPlugin : public te::Plugin, public IFaustEditorModel {
  public:
    FaustInstrumentPlugin(const te::PluginCreationInfo& info);
    ~FaustInstrumentPlugin() override;

    static const char* getPluginName() {
        return "Faust Instrument";
    }
    static const char* xmlTypeName;

    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "FaustInst";
    }
    juce::String getSelectableDescription() override {
        return getName();
    }

    void initialise(const te::PluginInitialisationInfo& info) override;
    void deinitialise() override;
    void reset() override;

    void applyToBuffer(const te::PluginRenderContext& fc) override;

    // Instrument reporting: consumes MIDI, generates audio, no audio input.
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

    void restorePluginStateFromValueTree(const juce::ValueTree&) override;

    // Compile `source`, wrap it in a fresh poly voice allocator, swap it in,
    // and persist source+name to plugin state. Returns true on success; on
    // failure `errorOut` carries the libfaust message and the previously
    // loaded DSP is left in place. Message thread only — the swap is atomic.
    bool loadDspSource(const juce::String& name, const juce::String& source,
                       juce::String& errorOut) override;

    void stageSourceForEditing(const juce::String& name, const juce::String& source) override;

    FaustPatchKind getPatchKind() const override {
        return FaustPatchKind::Instrument;
    }

    /**
     * @brief How incoming notes are allocated to voices.
     *
     * Owned by the host rather than by the patch. Voice allocation is the
     * host's job - it is what drives keyOn/keyOff and the reserved freq/gate
     * zones - so every runtime Faust instrument gets these modes whether or not
     * its .dsp knows they exist.
     */
    enum VoiceMode { Poly = 0, Mono = 1, Legato = 2 };

    // Host-owned parameters, addressed past the end of the [idx:N] pool so a
    // patch is still free to use all 64 of its own slots. See
    // faustInstrumentHostParamInfo().
    static constexpr int kVoiceModeParamIndex = FaustParamPool::kSize;
    static constexpr int kGlideParamIndex = FaustParamPool::kSize + 1;
    static constexpr int kBendRangeParamIndex = FaustParamPool::kSize + 2;
    static constexpr int kHostParamCount = 3;

    // Widest bend the range parameter can ask for, in semitones either way.
    static constexpr float kMaxBendSemitones = 24.0f;

    // Read access for the processor / parameter-info bridge.
    const FaustParamPool& getPool() const override {
        return pool_;
    }

    juce::String getDspName() const override {
        return dspName_;
    }

    juce::String getDspSource() const override {
        return dspSource_;
    }

    // See FaustPlugin::getPatchInfo - scanned on demand, never cached.
    FaustPatchInfo getPatchInfo() const override {
        return readPatchInfo(dspSource_);
    }

    juce::String getCustomViewName() const override {
        return viewName_;
    }

    const std::vector<juce::String>& getLastRebindDiagnostics() const override {
        return lastDiagnostics_;
    }

  private:
    // Per-state DSP bundle, atomically swapped on every successful load.
    // The audio thread snapshots it at the top of applyToBuffer; everything
    // inside is immutable for the snapshot's lifetime.
    struct FaustState {
        // mydsp_poly owns (and deletes) the per-voice DSP it was built from;
        // we delete the factory separately, after poly, in the destructor.
        std::unique_ptr<::dsp_poly> poly;
        dsp_factory* factory = nullptr;
        int dspIn = 0;
        int dspOut = 0;
        std::vector<FaustParamPool::ActiveBindingDescriptor> activeBindings;
        // Per pool-slot: the zone of that control in EVERY voice. group=false
        // gives each voice its own zones, so a user param write fans out to all
        // voices here (plain pointer writes — RT-safe, no global GUI state).
        // Indexed by slotIndex; empty for inactive slots.
        std::array<std::vector<FAUSTFLOAT*>, FaustParamPool::kSize> voiceZonesBySlot;

        // Dedicated single voice for Mono/Legato, built from the same factory
        // as the poly template. It has to be a separate instance rather than a
        // borrowed poly voice: the allocator owns which of its voices are live
        // and recycles idle ones, so a voice driven zone-only would be taken
        // out from under us.
        std::unique_ptr<::dsp> monoVoice;
        // The mono voice's reserved zones. Any may be null - a patch declaring
        // no `gain` is legitimate (see the Neuro Logical patch), and one
        // declaring no `gate` simply cannot be retriggered.
        FAUSTFLOAT* monoFreqZone = nullptr;
        FAUSTFLOAT* monoGainZone = nullptr;
        FAUSTFLOAT* monoGateZone = nullptr;
        // The mono voice's own copy of each pool slot's zone, so user params
        // reach it as well as the poly voices. One zone per slot, not a vector:
        // there is only ever one mono voice.
        std::array<FAUSTFLOAT*, FaustParamPool::kSize> monoZoneBySlot{};

        // Each poly voice's `freq` zones, parallel to the allocator's
        // fVoiceTable. Cached at compile time because applying pitch bend has
        // to rewrite them per block, and MapUI's own setter looks the zone up
        // by string - a map lookup we cannot afford on the audio thread.
        // Inner vector because a patch may declare `freq` more than once.
        std::vector<std::vector<FAUSTFLOAT*>> voiceFreqZones;

        ~FaustState();
    };

    static std::shared_ptr<FaustState> compile(const juce::String& source, int sampleRate,
                                               juce::String& errorOut);
    std::shared_ptr<FaustState> compileAndRebind(const juce::String& source,
                                                 juce::String& errorOut);
    void initialiseUnsetPoolValues(
        const std::vector<FaustParamPool::ActiveBindingDescriptor>& bindings,
        const std::array<FaustParamSlot, FaustParamPool::kSize>& previousSlots);

    // Active dsp + factory + binding bundle. Read/written via the std::shared_ptr
    // atomic free functions (libc++ lacks std::atomic<std::shared_ptr<T>>).
    std::shared_ptr<FaustState> active_;

    // Lifetime-stable parameter pool (macro/mod/automation links pin to slots).
    FaustParamPool pool_;
    std::vector<te::AutomatableParameter::Ptr> poolParams_;
    std::array<juce::CachedValue<float>, FaustParamPool::kSize> poolCached_;
    std::array<bool, FaustParamPool::kSize> poolValueWasRestored_{};

    // ---- Host-owned voice allocation -------------------------------------
    // Added after the pool params, so their TE parameter indices are
    // kVoiceModeParamIndex / kGlideParamIndex and nothing in the pool moves.
    te::AutomatableParameter::Ptr voiceModeParam_;
    te::AutomatableParameter::Ptr glideParam_;
    te::AutomatableParameter::Ptr bendRangeParam_;
    juce::CachedValue<float> voiceModeCached_;
    juce::CachedValue<float> glideCached_;
    juce::CachedValue<float> bendRangeCached_;

    int readVoiceMode() const;
    // Frequency multiplier for the current wheel position, 1.0 when centred.
    float readBendRatio() const;
    // Rewrite every sounding poly voice's freq zone for `ratio`. The allocator
    // writes an unbent freq on keyOn, so this also has to run after a note
    // starts, not only when the wheel moves.
    void applyBendToPolyVoices(const std::shared_ptr<FaustState>& state, float ratio);
    // Silence everything: poly voices, the mono voice, and the held-note stack.
    void resetAllVoices(const std::shared_ptr<FaustState>& state);
    // Release EVERY poly voice sounding (or legato-targeting) this pitch.
    // Unlike dsp_poly::keyOff, which releases only the oldest match and logs to
    // stderr when there is none, this is a silent no-op for unmatched offs and
    // self-heals orphans. Duplicate on/off streams for one pitch are legitimate
    // - a freshly recorded clip playing back while the live input that fed it
    // is still monitored - so an unbalanced delivery must never strand a voice.
    void releasePolyVoicesForPitch(const std::shared_ptr<FaustState>& state, int pitch);
    // Returns true when the caller must render one sample of gate-low before
    // raising the gate again, which is how Mono retriggers an envelope.
    bool handleMonoNoteOn(const std::shared_ptr<FaustState>& state, int note, int velocity,
                          int mode);
    void handleMonoNoteOff(const std::shared_ptr<FaustState>& state, int note);

    struct HeldNote {
        int note = 0;
        float gain = 0.0f;
    };
    // Mono/Legato note stack. Releasing a note returns to the one under it,
    // which is what makes trills and legato lines behave.
    std::vector<HeldNote> heldNotes_;

    // Glide state, in Hz. `glideCurrentHz_` is what the mono voice's freq zone
    // actually holds; it chases `glideTargetHz_`.
    float glideCurrentHz_ = 0.0f;
    float glideTargetHz_ = 0.0f;

    // Wheel position, -1..+1, owned by the audio thread. MIDI arrives in the
    // render context, so this never crosses a thread boundary.
    float bendNormalised_ = 0.0f;
    // Last ratio written to the poly voices, so a centred wheel costs nothing.
    float lastPolyBendRatio_ = 1.0f;

    int lastVoiceMode_ = Poly;
    bool wasPlaying_ = false;
    // reset() can arrive on the message thread while the audio thread is inside
    // compute(); walking the voice table from both is a race. reset() only
    // raises this and the flush runs at the top of the next applyToBuffer().
    std::atomic<bool> pendingVoiceFlush_{false};

    // Retired states pending destruction on the message thread (the audio
    // thread may briefly still hold a snapshot of the old state after a swap).
    struct RetiredItem {
        std::shared_ptr<FaustState> state;
        juce::uint32 retiredAtMs = 0;
    };
    juce::CriticalSection retiredLock_;
    std::vector<RetiredItem> retired_;

    class RetireTimer : public juce::Timer {
      public:
        explicit RetireTimer(FaustInstrumentPlugin& o) : owner(o) {}
        void timerCallback() override {
            owner.drainRetired();
        }
        FaustInstrumentPlugin& owner;
    };
    RetireTimer retireTimer_{*this};
    void drainRetired();

    juce::String dspName_;
    juce::String dspSource_;
    // Read from the loaded source's `declare magda_view`; empty for most patches.
    juce::String viewName_;
    std::vector<juce::String> lastDiagnostics_;

    int currentSampleRate_ = 44100;

    // Number of voices the poly allocator preallocates.
    static constexpr int kNumVoices = 16;

    // Scratch output for the poly compute(). Faust writes (overwrites) its
    // output buffers, so we render into scratch and then add into destBuffer
    // (synth semantics — the buffer may already carry signal we must not clobber).
    juce::AudioBuffer<float> scratchOut_;
    std::vector<float*> outPtrs_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FaustInstrumentPlugin)
};

}  // namespace magda::daw::audio
