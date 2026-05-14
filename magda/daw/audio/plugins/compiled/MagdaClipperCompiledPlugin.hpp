#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "../FaustParamPool.hpp"
#include "CompiledFaustInterface.hpp"
#include "core/ParameterInfo.hpp"

class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust multi-mode antialiased clipper.
 *
 * Static nonlinearity device — no envelope, attack, or release. The user
 * picks one of five ADAA curves from aa.lib (Hard / Soft / Tanh /
 * Hyperbolic / Sine) and drives the input into it.
 */
class MagdaClipperCompiledPlugin : public te::Plugin, public ICompiledFaustPlugin {
  public:
    static const char* xmlTypeName;

    explicit MagdaClipperCompiledPlugin(const te::PluginCreationInfo& info);
    ~MagdaClipperCompiledPlugin() override;

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

    static constexpr int kDriveSlot = 0;
    static constexpr int kModeSlot = 1;
    static constexpr int kOutputSlot = 2;
    static constexpr int kHostSlotCount = 3;

    enum class ClipperMode { Hard = 0, Soft, Tanh, Hyperbolic, Sine };
    static constexpr int kModeCount = 5;

    te::AutomatableParameter* getSlotParameter(int slotIndex) const;

    float displayValueToNativeValue(int slotIndex, float displayValue) const;
    float nativeValueToDisplayValue(int slotIndex, float nativeValue) const;

    // Audio-thread metering tap for the transfer-curve dot.
    float getInputPeakDb() const {
        return inputPeakDb_.load(std::memory_order_relaxed);
    }

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

    std::unique_ptr<::dsp> dsp_;
    int numInputs_ = 0;
    int numOutputs_ = 0;

    std::array<FAUSTFLOAT*, kHostSlotCount> zones_{};

    std::array<HostSlotInfo, kHostSlotCount> hostSlotInfo_;
    std::array<te::AutomatableParameter::Ptr, kHostSlotCount> hostParams_;
    std::array<juce::CachedValue<float>, kHostSlotCount> hostCached_;

    juce::AudioBuffer<float> scratchIn_;
    juce::AudioBuffer<float> scratchOut_;
    std::vector<float*> inPtrs_;
    std::vector<float*> outPtrs_;

    std::atomic<float> inputPeakDb_{-120.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaClipperCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
