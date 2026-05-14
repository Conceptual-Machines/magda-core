#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "compiled/CompiledPluginPresentation.hpp"
#include "core/DeviceInfo.hpp"

namespace magda::daw::audio::compiled {
class MagdaLimiterCompiledPlugin;
}

namespace magda::daw::ui {

/**
 * @brief Visualisation for the compiled limiter.
 *
 * Shows input + output level meters side-by-side with the user-set
 * threshold drawn across both as a ceiling line. A gain-reduction bar to
 * the right reads the per-block input/output dB difference. Threshold is
 * draggable horizontally on the input meter.
 */
class CompiledLimiterCurveView final : public juce::Component,
                                       public CompiledDevicePanel,
                                       private juce::Timer {
  public:
    explicit CompiledLimiterCurveView(juce::String pluginId);

    int getPreferredHeight() const {
        return 146;
    }

    void setCompiledPlugin(magda::daw::audio::compiled::MagdaLimiterCompiledPlugin* plugin);
    void updateFromDevice(const magda::DeviceInfo& device) override;

    juce::Component& component() override {
        return *this;
    }
    void bindPlugin(te::Plugin* plugin) override;
    void setOnParameterChanged(std::function<void(int, float)> cb) override {
        onParameterChanged = std::move(cb);
    }
    int preferredHeight() const override {
        return getPreferredHeight();
    }

    std::function<void(int slotIndex, float displayValue)> onParameterChanged;

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

  private:
    enum class Handle { None, Threshold };

    void timerCallback() override;
    void resampleFromDevice();

    float yToDb(float y) const;
    float dbToY(float db) const;
    Handle pickHandle(float y) const;

    magda::daw::audio::compiled::MagdaLimiterCompiledPlugin* compiledPlugin_ = nullptr;
    magda::DeviceInfo deviceSnapshot_;

    float thresholdDb_ = -1.0f;
    float attackMs_ = 1.0f;
    float releaseMs_ = 200.0f;

    float inputPeakDb_ = -120.0f;
    float outputPeakDb_ = -120.0f;
    float gainReductionDb_ = 0.0f;

    // Envelope-followed display values so the meters move with peak-meter
    // feel instead of per-block jitter.
    float smoothedInputPeakDb_ = -120.0f;
    float smoothedOutputPeakDb_ = -120.0f;
    float smoothedGainReductionDb_ = 0.0f;

    juce::Rectangle<float> meterArea_;

    Handle hoveredHandle_ = Handle::None;
    Handle draggedHandle_ = Handle::None;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledLimiterCurveView)
};

}  // namespace magda::daw::ui
