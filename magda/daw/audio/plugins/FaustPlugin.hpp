#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "FaustParamPool.hpp"
#include "IFaustEditorModel.hpp"
#include "core/ParameterUtils.hpp"
#include "plugins/MagdaDevice.hpp"

// libfaust types are forward-declared here so consumers don't need the Faust
// runtime headers on their include path. Implementation pulls them in.
class dsp;
class dsp_factory;

namespace magda::daw::audio {

// Hosts a libfaust interpreter-compiled DSP. The .dsp source is held in
// plugin state and (re)compiled at construction / on user load.
//
// Parameters live in a fixed pool of FaustParamPool::kSize stable slots
// created at construction time. Each slot is normalized 0..1 and persists for
// the device's lifetime; on a DSP swap the live controls are routed into slots
// and the audio thread denormalizes per-slot to real units when writing the
// zone. This keeps macro / mod / MIDI Learn / automation links pinned to slot
// indices that survive a recompile — see docs/architecture/faust-param-pool.md.
class FaustPlugin : public MagdaDevice, public IFaustEditorModel {
  public:
    FaustPlugin();
    ~FaustPlugin() override;

    static const char* getPluginName() {
        return "Faust";
    }
    static const char* xmlTypeName;

    DeviceProperties properties() const override;

    void prepare(const DevicePrepareContext& context) override;
    void release() override;
    void reset() override;
    void process(DeviceProcessContext& context) override;

    int parameterCount() const override {
        return FaustParamPool::kSize;
    }
    ParameterInfo parameterInfo(int index) const override;
    float parameterValue(int index) const override;
    void setParameterValue(int index, float value) override;

    void flushState(juce::ValueTree& state) override;
    void restoreState(const juce::ValueTree& state) override;

  private:
    /// Re-cache the conversion domain of every pool slot. Called whenever the
    /// slot table changes, so process() never builds one.
    void refreshPoolDomains();

  public:
    // Compile `source` with the interpreter backend, swap in the new DSP,
    // and persist source+name to plugin state. Returns true on success;
    // on failure `errorOut` carries the libfaust error message and the
    // previously-loaded DSP (if any) is left in place. Safe to call from
    // the message thread while the audio thread is processing — the
    // FaustState swap is atomic.
    bool loadDspSource(const juce::String& name, const juce::String& source,
                       juce::String& errorOut) override;

    // Stage source into the editable state WITHOUT compiling or swapping the
    // live DSP. The code editor reads `dspSource` from state, so this puts
    // generated code in front of the user to review and compile manually.
    // Used when AI-generated code can't be auto-verified (faust-mcp off).
    // Message thread only.
    void stageSourceForEditing(const juce::String& name, const juce::String& source) override;

    FaustPatchKind getPatchKind() const override {
        return FaustPatchKind::Effect;
    }

    // Read access for the UI / parameter-info bridge (Phase 4b). The
    // pool's slot table is mutated only by `loadDspSource` on the
    // message thread.
    const FaustParamPool& getPool() const override {
        return pool_;
    }

    // False when a saved source failed to compile and the live DSP is only
    // the fallback. Used during project load to preserve routing that may be
    // valid again once the missing library/source problem is repaired.
    bool activeDspMatchesSource() const {
        return activeDspMatchesSource_;
    }

    // Per-DSP display name (caller-supplied to `loadDspSource`). Used
    // for the inspector label only — the FaustUI custom-view registry
    // keys on `getCustomViewName()` instead.
    juce::String getDspName() const override {
        return dspName_;
    }

    // IFaustEditorModel: the live .dsp source the code editor reads/edits.
    juce::String getDspSource() const override {
        return dspSource_;
    }

    // Scanned from the live source on demand rather than cached, so it can
    // never go stale against a recompile. Called when the UI binds or
    // refreshes, not per frame, so the handful of regexes is cheap enough.
    FaustPatchInfo getPatchInfo() const override {
        return readPatchInfo(dspSource_);
    }

    // Name of the bespoke FaustUI view this DSP asked for, or empty if
    // it asked for none. Derived from the source's `declare magda_view`
    // on every load, so a bundled starter, a file-picker load and an
    // editor recompile all resolve identically.
    juce::String getCustomViewName() const override {
        return viewName_;
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
        dsp_factory* factory = nullptr;
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

    // Lifetime-stable parameter pool. The slot table is mutated on the message
    // thread; the values below are read wait-free from the audio thread.
    FaustParamPool pool_;
    // Normalized 0..1 per slot, and the domain each was last bound with. The
    // domain is cached because process() denormalizes every active binding
    // every block, and building a ParameterInfo there would allocate.
    std::array<std::atomic<float>, FaustParamPool::kSize> poolValues_{};
    std::array<ParameterUtils::ParameterDomain, FaustParamPool::kSize> poolDomains_{};
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
    // Read from the loaded source's `declare magda_view`; empty for most patches.
    juce::String viewName_;
    std::vector<juce::String> lastDiagnostics_;
    bool activeDspMatchesSource_ = false;

    // Sample rate captured from initialise(); used when recompiling at
    // runtime (constructor uses 44100 as a provisional value).
    int currentSampleRate_ = 44100;

    // Scratch buffer for Faust inputs. Faust's compute() does not permit
    // aliasing inputs/outputs unless the .dsp is compiled with -inpl, so
    // we copy the incoming audio into a separate scratch before calling.
    juce::AudioBuffer<float> scratchIn_;
    // applyToBuffer resize()s these every block. Reserving on the message
    // thread whenever a DSP is installed means that resize can never grow
    // capacity, and so never allocates on the audio thread.
    void reservePointerScratch(const std::shared_ptr<FaustState>& state);
    std::vector<float*> inPtrs_;
    std::vector<float*> outPtrs_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FaustPlugin)
};

}  // namespace magda::daw::audio
