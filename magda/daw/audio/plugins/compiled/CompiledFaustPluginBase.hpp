#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <memory>
#include <vector>

#include "../FaustParamPool.hpp"

class dsp;

namespace magda::daw::audio::compiled {

namespace te = tracktion::engine;

class CompiledFaustPluginBase : public te::Plugin {
  public:
    CompiledFaustPluginBase(const te::PluginCreationInfo& info, std::unique_ptr<::dsp> dsp,
                            juce::String displayName, juce::String xmlType);
    ~CompiledFaustPluginBase() override;

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

    const FaustParamPool& getPool() const {
        return pool_;
    }

    float displayValueToNativeValue(int slotIndex, float displayValue) const;
    float nativeValueToDisplayValue(int slotIndex, float nativeValue) const;

  private:
    void harvestAndCreateParameters();
    void ensureScratchBuffer(int blockSize);

    std::unique_ptr<::dsp> dsp_;
    juce::String displayName_;
    juce::String xmlType_;

    FaustParamPool pool_;
    std::vector<FaustParamPool::ActiveBindingDescriptor> activeBindings_;
    std::array<te::AutomatableParameter::Ptr, FaustParamPool::kSize> slotParams_;
    std::array<juce::CachedValue<float>, FaustParamPool::kSize> slotValues_;

    juce::AudioBuffer<float> scratchIn_;
    juce::AudioBuffer<float> scratchOut_;
    std::vector<float*> inPtrs_;
    std::vector<float*> outPtrs_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledFaustPluginBase)
};

class MagdaSVFCompiledPlugin final : public CompiledFaustPluginBase {
  public:
    static const char* xmlTypeName;
    explicit MagdaSVFCompiledPlugin(const te::PluginCreationInfo& info);
};

class MagdaLadderCompiledPlugin final : public CompiledFaustPluginBase {
  public:
    static const char* xmlTypeName;
    explicit MagdaLadderCompiledPlugin(const te::PluginCreationInfo& info);
};

class MagdaKorg35CompiledPlugin final : public CompiledFaustPluginBase {
  public:
    static const char* xmlTypeName;
    explicit MagdaKorg35CompiledPlugin(const te::PluginCreationInfo& info);
};

class MagdaOberheimCompiledPlugin final : public CompiledFaustPluginBase {
  public:
    static const char* xmlTypeName;
    explicit MagdaOberheimCompiledPlugin(const te::PluginCreationInfo& info);
};

class MagdaSallenKeyCompiledPlugin final : public CompiledFaustPluginBase {
  public:
    static const char* xmlTypeName;
    explicit MagdaSallenKeyCompiledPlugin(const te::PluginCreationInfo& info);
};

}  // namespace magda::daw::audio::compiled
