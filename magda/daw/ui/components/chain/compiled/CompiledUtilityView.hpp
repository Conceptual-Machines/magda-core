#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

#include "compiled/CompiledPluginPresentation.hpp"
#include "core/DeviceInfo.hpp"

namespace magda::daw::audio::compiled {
class MagdaUtilityCompiledPlugin;
}

namespace magda::daw::ui {

class CompiledUtilityView final : public juce::Component,
                                  public CompiledDevicePanel,
                                  private juce::Timer {
  public:
    explicit CompiledUtilityView(juce::String pluginId);
    ~CompiledUtilityView() override;

    void setCompiledPlugin(magda::daw::audio::compiled::MagdaUtilityCompiledPlugin* plugin);
    void updateFromDevice(const magda::DeviceInfo& device) override;

    juce::Component& component() override {
        return *this;
    }
    void bindPlugin(te::Plugin* plugin) override;
    void setOnParameterChanged(std::function<void(int, float)> cb) override {
        onParameterChanged = std::move(cb);
    }
    int preferredHeight() const override {
        return 220;
    }

    std::function<void(int slotIndex, float displayValue)> onParameterChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    void timerCallback() override;
    void syncFromDevice();

    struct ButtonLaf : public juce::LookAndFeel_V4 {
        juce::Font getTextButtonFont(juce::TextButton&, int) override;
    };

    magda::daw::audio::compiled::MagdaUtilityCompiledPlugin* compiledPlugin_ = nullptr;
    magda::DeviceInfo deviceSnapshot_;
    ButtonLaf buttonLaf_;

    juce::Slider gainSlider_;
    juce::Slider panSlider_;
    juce::Slider widthSlider_;
    juce::Slider lowXoverSlider_;

    std::array<juce::TextButton, 4> btns_;
    static constexpr std::array<const char*, 4> kLabels{"MONO", "LOW MONO", "FLIP L", "FLIP R"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledUtilityView)
};

}  // namespace magda::daw::ui
