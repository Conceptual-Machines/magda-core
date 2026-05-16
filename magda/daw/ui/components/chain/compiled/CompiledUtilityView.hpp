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

    int getPreferredHeight() const {
        return 48;
    }

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
        return getPreferredHeight();
    }

    std::function<void(int slotIndex, float displayValue)> onParameterChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    void timerCallback() override;
    void resampleFromDevice();
    void updateButtonState(int btnIdx, bool on);

    magda::daw::audio::compiled::MagdaUtilityCompiledPlugin* compiledPlugin_ = nullptr;
    magda::DeviceInfo deviceSnapshot_;

    // 4 toggle buttons corresponding to slots 4-7: Mono, Low Mono, Flip L, Flip R
    std::array<juce::TextButton, 4> btns_;
    static constexpr std::array<const char*, 4> kLabels{"MONO", "LOW MONO", "FLIP L", "FLIP R"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledUtilityView)
};

}  // namespace magda::daw::ui
