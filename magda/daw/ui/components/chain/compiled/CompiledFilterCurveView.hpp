#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "compiled/CompiledPluginPresentation.hpp"
#include "core/DeviceInfo.hpp"
#include "params/ParamLinkResolver.hpp"

namespace magda::daw::audio::compiled {
class MagdaFilterCompiledPlugin;
}

namespace magda::daw::ui {

class CompiledFilterCurveView final : public juce::Component,
                                      public CompiledDevicePanel,
                                      private juce::Timer {
  public:
    explicit CompiledFilterCurveView(juce::String pluginId);

    int getPreferredHeight() const {
        return 140;
    }

    void updateFromDevice(const magda::DeviceInfo& device,
                          const ParamLinkContext* linkContext) override;
    void setCompiledPlugin(magda::daw::audio::compiled::MagdaFilterCompiledPlugin* plugin);

    // CompiledDevicePanel
    juce::Component& component() override {
        return *this;
    }
    void updateFromDevice(const magda::DeviceInfo& device) override {
        updateFromDevice(device, nullptr);
    }
    void bindPlugin(te::Plugin* plugin) override;
    void setOnParameterChanged(std::function<void(int, float)>) override {}  // read-only view
    int preferredHeight() const override {
        return getPreferredHeight();
    }

    void paint(juce::Graphics& g) override;

  private:
    enum class FilterFamily { SVF, Ladder, Korg35, Oberheim, SallenKey };
    enum class FilterMode { LowPass, BandPass, HighPass, Notch };

    FilterFamily family_ = FilterFamily::SVF;
    float cutoffHz_ = 1000.0f;
    float resonance_ = 0.0f;
    float drive_ = 0.0f;
    int modeIndex_ = 0;
    float targetCutoffHz_ = 1000.0f;
    float targetResonance_ = 0.0f;
    float targetDrive_ = 0.0f;
    int targetModeIndex_ = 0;
    bool initialised_ = false;
    magda::DeviceInfo deviceSnapshot_;
    ParamLinkContext linkContext_;
    bool hasLinkContext_ = false;
    magda::daw::audio::compiled::MagdaFilterCompiledPlugin* compiledPlugin_ = nullptr;

    FilterMode modeForIndex() const;
    float responseDbAt(float frequencyHz) const;
    float qValue() const;
    void updateTargetValues();
    bool hasActiveCurveLinks() const;
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledFilterCurveView)
};

}  // namespace magda::daw::ui
