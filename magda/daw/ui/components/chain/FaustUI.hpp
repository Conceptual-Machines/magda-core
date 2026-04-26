#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace tracktion { inline namespace engine { class AutomatableParameter; }}

namespace magda::daw::audio {
class FaustPlugin;
}

namespace magda::daw::ui {

// Minimal generic UI for FaustPlugin (Stage 1 POC). Builds a row of sliders
// from whatever AutomatableParameters the plugin currently exposes — works for
// any .dsp the FaustPlugin happens to host. No macro/mod / automation-record
// integration; sliders write directly to the parameter.
class FaustUI : public juce::Component, private juce::Timer {
  public:
    FaustUI();
    ~FaustUI() override;

    void setPlugin(magda::daw::audio::FaustPlugin* plugin);

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    void timerCallback() override;
    void rebuildFromPlugin();

    struct ParamSlot {
        juce::Label label;
        juce::Slider slider;
        tracktion::engine::AutomatableParameter* param = nullptr;
    };

    magda::daw::audio::FaustPlugin* plugin_ = nullptr;
    std::vector<std::unique_ptr<ParamSlot>> slots_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FaustUI)
};

}  // namespace magda::daw::ui
