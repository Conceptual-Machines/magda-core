#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "FaustCustomViewKind.hpp"
#include "FaustParamPool.hpp"
#include "IFaustEditorModel.hpp"

// libfaust types are forward-declared so consumers don't need the Faust
// runtime headers on their include path. Implementation pulls them in.
class interpreter_dsp_factory;
class dsp_poly;

namespace magda::daw::audio {

namespace te = tracktion::engine;

// POC: a Faust-based polyphonic *instrument*. Sibling of FaustPlugin (the
// effect host) — it shares the same runtime-compile + FaustParamPool design,
// but reports as a synth, wraps the compiled DSP in a polyphonic voice
// allocator (mydsp_poly), and drives note allocation from the MIDI in the
// PluginRenderContext. The .dsp source is expected to follow the Faust
// polyphonic convention: the reserved controls `freq`, `gain`, `gate` are
// driven per-voice by the allocator and are NOT exposed as user parameters.
//
// Shared scaffolding (UIHarvester, pool rebind, atomic state swap, retire
// timer) is copied from FaustPlugin for the POC; a future refactor can
// extract a common base. See the plan / docs/FAUST_POOL_REFACTOR.md.
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
    bool loadDspSource(const juce::String& name, const juce::String& source, juce::String& errorOut,
                       FaustCustomViewKind viewKind = FaustCustomViewKind::None) override;

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

    FaustCustomViewKind getCustomViewKind() const override {
        return viewKind_;
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
        interpreter_dsp_factory* factory = nullptr;
        int dspIn = 0;
        int dspOut = 0;
        std::vector<FaustParamPool::ActiveBindingDescriptor> activeBindings;
        // Per pool-slot: the zone of that control in EVERY voice. group=false
        // gives each voice its own zones, so a user param write fans out to all
        // voices here (plain pointer writes — RT-safe, no global GUI state).
        // Indexed by slotIndex; empty for inactive slots.
        std::array<std::vector<FAUSTFLOAT*>, FaustParamPool::kSize> voiceZonesBySlot;
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
    FaustCustomViewKind viewKind_ = FaustCustomViewKind::None;
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
