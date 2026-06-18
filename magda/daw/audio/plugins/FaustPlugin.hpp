#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "FaustCustomViewKind.hpp"
#include "FaustParamPool.hpp"
#include "IFaustEditorModel.hpp"

// libfaust types are forward-declared here so consumers don't need the Faust
// runtime headers on their include path. Implementation pulls them in.
class dsp;
class interpreter_dsp_factory;

namespace magda::daw::audio {

namespace te = tracktion::engine;

// Hosts a libfaust interpreter-compiled DSP. The .dsp source is held in
// plugin state and (re)compiled at construction / on user load.
//
// Parameters live in a fixed pool of FaustParamPool::kSize stable
// AutomatableParameters created at construction time. Each slot's
// AutomatableParameter is normalized 0..1 and persists for the
// plugin's lifetime; on a DSP swap the live controls are routed into
// slots and the audio thread denormalizes per-slot to real units when
// writing the zone. This keeps macro / mod / MIDI Learn / automation
// links pinned to slot indices that survive a recompile — see
// docs/FAUST_POOL_REFACTOR.md.
class FaustPlugin : public te::Plugin, public IFaustEditorModel {
  public:
    FaustPlugin(const te::PluginCreationInfo& info);
    ~FaustPlugin() override;

    static const char* getPluginName() {
        return "Faust";
    }
    static const char* xmlTypeName;

    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "Faust";
    }
    juce::String getSelectableDescription() override {
        return getName();
    }

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

    void restorePluginStateFromValueTree(const juce::ValueTree&) override;

    // Compile `source` with the interpreter backend, swap in the new DSP,
    // and persist source+name to plugin state. Returns true on success;
    // on failure `errorOut` carries the libfaust error message and the
    // previously-loaded DSP (if any) is left in place. Safe to call from
    // the message thread while the audio thread is processing — the
    // FaustState swap is atomic.
    bool loadDspSource(const juce::String& name, const juce::String& source, juce::String& errorOut,
                       FaustCustomViewKind viewKind = FaustCustomViewKind::None) override;

    // Stage source into the editable state WITHOUT compiling or swapping the
    // live DSP. The code editor reads `dspSource` from state, so this puts
    // generated code in front of the user to review and compile manually.
    // Used when AI-generated code can't be auto-verified (faust-mcp off).
    // Message thread only.
    void stageSourceForEditing(const juce::String& name, const juce::String& source);

    // Read access for the UI / parameter-info bridge (Phase 4b). The
    // pool's slot table is mutated only by `loadDspSource` on the
    // message thread.
    const FaustParamPool& getPool() const override {
        return pool_;
    }

    // Per-DSP display name (caller-supplied to `loadDspSource`). Used
    // for the inspector label only — the FaustUI custom-view registry
    // keys on `getCustomViewKind()` instead.
    juce::String getDspName() const override {
        return dspName_;
    }

    // IFaustEditorModel: the live .dsp source the code editor reads/edits.
    juce::String getDspSource() const override {
        return dspSource_;
    }

    // Identifier for the bespoke FaustUI view registered against this
    // DSP, or `None` if there isn't one. Set on `loadDspSource`;
    // defaults to `None` for the constructor's passthrough DSP and
    // for user-loaded files (file picker / code editor). Stable
    // across the plugin's lifetime within one loaded DSP.
    FaustCustomViewKind getCustomViewKind() const override {
        return viewKind_;
    }

    // Diagnostics from the most recent rebind (overflow / duplicate idx
    // / out-of-range). UI surfaces these in the FaustUI error label.
    // Read on the message thread.
    const std::vector<juce::String>& getLastRebindDiagnostics() const override {
        return lastDiagnostics_;
    }

  private:
    // Per-state DSP bundle, atomically swapped on every successful
    // load. The audio thread takes a snapshot at the top of
    // `applyToBuffer`; everything inside the bundle is immutable for
    // the snapshot's lifetime.
    struct FaustState {
        std::unique_ptr<::dsp> dsp;
        interpreter_dsp_factory* factory = nullptr;
        int dspIn = 0;
        int dspOut = 0;
        // Audio-thread view of the active slots: which pool slot →
        // which zone, plus the denormalization metadata frozen at
        // compile time. Built by `compileAndRebind` and never
        // mutated after the state is published.
        std::vector<FaustParamPool::ActiveBindingDescriptor> activeBindings;
        ~FaustState();
    };

    static std::shared_ptr<FaustState> compile(const juce::String& source, int sampleRate,
                                               juce::String& errorOut);
    // Compile + harvest + pool rebind, returning the fresh state.
    // Stores diagnostics on `lastDiagnostics_`. Message-thread only.
    std::shared_ptr<FaustState> compileAndRebind(const juce::String& source,
                                                 juce::String& errorOut);
    void initialiseUnsetPoolValues(
        const std::vector<FaustParamPool::ActiveBindingDescriptor>& bindings,
        const std::array<FaustParamSlot, FaustParamPool::kSize>& previousSlots);

    // Active dsp + factory + binding bundle. Read/written exclusively via
    // std::atomic_load / std::atomic_store free functions on shared_ptr —
    // libc++ does not yet implement std::atomic<std::shared_ptr<T>>.
    std::shared_ptr<FaustState> active_;

    // Lifetime-stable parameter pool. Slot table is mutated on the
    // message thread; AutomatableParameter values are read wait-free
    // from the audio thread via `getCurrentValue()`.
    FaustParamPool pool_;
    // CachedValue is non-copyable/non-movable — kept in a std::array
    // (fixed-size, in-place) rather than a vector. AutomatableParameter
    // pointers are reference-counted, vector is fine.
    std::vector<te::AutomatableParameter::Ptr> poolParams_;
    std::array<juce::CachedValue<float>, FaustParamPool::kSize> poolCached_;
    std::array<bool, FaustParamPool::kSize> poolValueWasRestored_{};

    // Retired states pending destruction on the message thread. After a
    // swap, the audio thread may briefly still hold a snapshot of the
    // old state; we park it here and let RetireTimer drop it after a
    // delay long enough for any in-flight audio buffer to finish.
    struct RetiredItem {
        std::shared_ptr<FaustState> state;
        juce::uint32 retiredAtMs = 0;
    };
    juce::CriticalSection retiredLock_;
    std::vector<RetiredItem> retired_;

    class RetireTimer : public juce::Timer {
      public:
        explicit RetireTimer(FaustPlugin& o) : owner(o) {}
        void timerCallback() override {
            owner.drainRetired();
        }
        FaustPlugin& owner;
    };
    RetireTimer retireTimer_{*this};
    void drainRetired();

    juce::String dspName_;
    juce::String dspSource_;
    FaustCustomViewKind viewKind_ = FaustCustomViewKind::None;
    std::vector<juce::String> lastDiagnostics_;

    // Sample rate captured from initialise(); used when recompiling at
    // runtime (constructor uses 44100 as a provisional value).
    int currentSampleRate_ = 44100;

    // Scratch buffer for Faust inputs. Faust's compute() does not permit
    // aliasing inputs/outputs unless the .dsp is compiled with -inpl, so
    // we copy the incoming audio into a separate scratch before calling.
    juce::AudioBuffer<float> scratchIn_;
    std::vector<float*> inPtrs_;
    std::vector<float*> outPtrs_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FaustPlugin)
};

}  // namespace magda::daw::audio
