#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/DeviceInfo.hpp"

namespace magda::daw::ui {

class CompiledFilterCurveView final : public juce::Component {
  public:
    explicit CompiledFilterCurveView(juce::String pluginId);

    int getPreferredHeight() const {
        return 92;
    }

    void updateFromDevice(const magda::DeviceInfo& device);
    void paint(juce::Graphics& g) override;

  private:
    enum class FilterFamily { SVF, Ladder, Korg35, Oberheim, SallenKey };
    enum class FilterMode { LowPass, BandPass, HighPass, Notch };

    FilterFamily family_ = FilterFamily::SVF;
    float cutoffHz_ = 1000.0f;
    float resonance_ = 0.0f;
    float drive_ = 0.0f;
    int modeIndex_ = 0;

    FilterMode modeForIndex() const;
    float responseDbAt(float frequencyHz) const;
    float qValue() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledFilterCurveView)
};

}  // namespace magda::daw::ui
