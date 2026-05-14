#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "compiled/CompiledPluginPresentation.hpp"
#include "core/DeviceInfo.hpp"

namespace magda::daw::audio::compiled {
class MagdaClipperCompiledPlugin;
}

namespace magda::daw::ui {

/**
 * @brief Transfer-curve visualisation for the compiled clipper.
 *
 * Plots input on X / output on Y for the active Mode, with the curve
 * recomputed whenever Mode changes. A dot rides the curve at the
 * post-Drive input peak so the user can see where the signal is sitting
 * on the nonlinearity at a glance.
 */
class CompiledClipperCurveView final : public juce::Component,
                                       public CompiledDevicePanel,
                                       private juce::Timer {
  public:
    explicit CompiledClipperCurveView(juce::String pluginId);

    int getPreferredHeight() const {
        return 120;
    }

    void setCompiledPlugin(magda::daw::audio::compiled::MagdaClipperCompiledPlugin* plugin);
    void updateFromDevice(const magda::DeviceInfo& device) override;

    juce::Component& component() override {
        return *this;
    }
    void bindPlugin(te::Plugin* plugin) override;
    void setOnParameterChanged(std::function<void(int, float)>) override {}
    int preferredHeight() const override {
        return getPreferredHeight();
    }

    void paint(juce::Graphics& g) override;

  private:
    void timerCallback() override;
    void resampleFromDevice();

    magda::daw::audio::compiled::MagdaClipperCompiledPlugin* compiledPlugin_ = nullptr;
    magda::DeviceInfo deviceSnapshot_;

    int mode_ = 0;
    float driveDb_ = 0.0f;
    float inputPeakDb_ = -120.0f;
    float smoothedInputAmp_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledClipperCurveView)
};

}  // namespace magda::daw::ui
