#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <memory>
#include <vector>

// The Faust runtime and generated DSP headers are deliberately kept out of this
// header so consumers don't need third_party/faust-runtime on their include
// path. Everything Faust-specific lives in FaustPlugin.cpp.
class MagdaDriveDsp;

namespace magda::daw::audio {

namespace te = tracktion::engine;

// Stage 1 Faust DSP integration POC: hosts a single AOT-generated Faust class
// (MagdaDriveDsp) as a native MAGDA effect plugin. Parameters are harvested from
// the DSP's buildUserInterface() visitor and registered as AutomatableParameters
// so the stock UI auto-renders sliders. Saves/restores via ValueTree like any
// other plugin. See plans/dazzling-crunching-prism.md for scope.
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

  private:
    // One per Faust slider. `zone` is a pointer into faustDsp_'s member storage
    // (what Faust calls the "zone" for a control) — never free it here.
    // Typed as float* because FAUSTFLOAT defaults to float and using the raw
    // type keeps the Faust headers out of this file.
    struct ParamBinding {
        juce::String id;
        juce::String label;
        juce::CachedValue<float> cached;
        te::AutomatableParameter::Ptr param;
        float* zone = nullptr;
    };

    std::unique_ptr<MagdaDriveDsp> faustDsp_;
    std::vector<std::unique_ptr<ParamBinding>> bindings_;

    // Scratch buffer for Faust inputs. Faust's compute() does not permit
    // aliasing inputs/outputs unless the .dsp is compiled with -inpl, so we
    // copy the incoming audio into a separate scratch before calling compute().
    juce::AudioBuffer<float> scratchIn_;
    std::vector<float*> inPtrs_;
    std::vector<float*> outPtrs_;
    int scratchChannels_ = 0;

    int dspIn_ = 0;
    int dspOut_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FaustPlugin)
};

}  // namespace magda::daw::audio
