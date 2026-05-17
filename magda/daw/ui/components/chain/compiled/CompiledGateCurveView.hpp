#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "compiled/CompiledPluginPresentation.hpp"
#include "core/DeviceInfo.hpp"

namespace magda::daw::audio::compiled {
class MagdaGateExpanderCompiledPlugin;
}

namespace magda::daw::ui {

class CompiledGateCurveView final : public juce::Component,
                                    public CompiledDevicePanel,
                                    private juce::Timer {
  public:
    explicit CompiledGateCurveView(juce::String pluginId);

    int getPreferredHeight() const {
        return 146;
    }

    void setCompiledPlugin(magda::daw::audio::compiled::MagdaGateExpanderCompiledPlugin* plugin);
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
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

  private:
    enum class Handle { None, Threshold };

    void timerCallback() override;
    void resampleFromDevice();

    float xToDb(float x) const;
    float dbToX(float db) const;
    float dbToY(float db) const;
    Handle pickHandle(float x) const;

    magda::daw::audio::compiled::MagdaGateExpanderCompiledPlugin* compiledPlugin_ = nullptr;
    magda::DeviceInfo deviceSnapshot_;

    float thresholdDb_ = -40.0f;
    float ratio_ = 4.0f;
    float rangeDb_ = 60.0f;

    Handle hoveredHandle_ = Handle::None;
    Handle draggedHandle_ = Handle::None;
    juce::Rectangle<float> plotArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledGateCurveView)
};

}  // namespace magda::daw::ui
