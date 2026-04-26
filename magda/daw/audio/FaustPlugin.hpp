#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <atomic>
#include <map>
#include <memory>
#include <vector>

// libfaust types are forward-declared here so consumers don't need the Faust
// runtime headers on their include path. Implementation pulls them in.
class dsp;
class interpreter_dsp_factory;

namespace magda::daw::audio {

namespace te = tracktion::engine;

// Stage 2: hosts a libfaust interpreter-compiled DSP. The .dsp source is held
// in plugin state and (re)compiled at construction / on user load. Parameters
// are harvested from buildUserInterface() and registered as
// AutomatableParameters so the stock UI auto-renders sliders.
class FaustPlugin : public te::Plugin {
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

    // Compile `source` with the interpreter backend, swap in the new DSP, and
    // persist source+name to plugin state. Returns true on success; on failure
    // `errorOut` carries the libfaust error message and the previously loaded
    // DSP (if any) is left in place. Safe to call from the message thread
    // while the audio thread is processing — the swap is atomic.
    bool loadDspSource(const juce::String& name, const juce::String& source,
                       juce::String& errorOut);

  private:
    // One per Faust slider. `zone` points into the active dsp's member storage
    // — never freed here; lifetime is tied to the FaustState that owns the dsp.
    struct ParamBinding {
        juce::String id;
        juce::String label;
        juce::CachedValue<float> cached;
        te::AutomatableParameter::Ptr param;
        float* zone = nullptr;
    };

    // Bundle of state owned together: factory, dsp instance, and the slider
    // zones (whose pointers belong to the dsp instance). Held via
    // shared_ptr so the audio thread can take an atomic snapshot and use it
    // for the duration of a buffer; UI-thread loadDspSource() atomically
    // replaces it. ~FaustState may run on the audio thread when the snapshot
    // drops its last ref — acceptable POC limitation; production would defer.
    struct FaustState {
        std::unique_ptr<::dsp> dsp;
        interpreter_dsp_factory* factory = nullptr;
        std::vector<float*> zones;  // parallel to bindings_; one per slider
        int dspIn = 0;
        int dspOut = 0;
        ~FaustState();
    };

    // Compile `source` into a fresh FaustState. Returns nullptr on failure
    // and writes the libfaust error to `errorOut`.
    static std::shared_ptr<FaustState> compile(const juce::String& source,
                                                int sampleRate,
                                                juce::String& errorOut);

    // Rebuild the AutomatableParameter set + ParamBinding list to match the
    // sliders in `state->dsp`. Called after a successful compile, on the
    // message thread. Detaches old bindings before installing new ones.
    void rebuildParameters(const std::shared_ptr<FaustState>& state);

    // Active dsp + factory bundle. Read/written exclusively via
    // std::atomic_load / std::atomic_store free functions on shared_ptr —
    // libc++ does not yet implement std::atomic<std::shared_ptr<T>>.
    std::shared_ptr<FaustState> active_;
    std::vector<std::unique_ptr<ParamBinding>> bindings_;

    // Retired states pending destruction on the message thread. After a swap,
    // the audio thread may briefly still hold a snapshot of the old state; we
    // park it here and let RetireTimer drop it after a delay long enough for
    // any in-flight audio buffer to finish. Without this, dropping the last
    // ref on the audio thread runs the dsp/factory dtor there.
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

    // Slider values seen for each DSP source we've loaded this session, so
    // a swap chain like Drive→Tremolo→Drive restores Drive's tweaks instead
    // of resetting to defaults. Snapshot is captured on every swap-out and
    // re-applied on swap-in. Not persisted across project loads — those go
    // through the per-plugin ValueTree like before.
    std::map<juce::String, std::map<juce::String, float>> savedValuesBySource_;

    // Sample rate captured from initialise(); used when recompiling at
    // runtime (constructor uses 44100 as a provisional value).
    int currentSampleRate_ = 44100;

    // Scratch buffer for Faust inputs. Faust's compute() does not permit
    // aliasing inputs/outputs unless the .dsp is compiled with -inpl, so we
    // copy the incoming audio into a separate scratch before calling compute().
    juce::AudioBuffer<float> scratchIn_;
    std::vector<float*> inPtrs_;
    std::vector<float*> outPtrs_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FaustPlugin)
};

}  // namespace magda::daw::audio
