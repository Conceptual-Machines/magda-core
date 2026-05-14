#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "audio/plugins/compiled/MagdaPitchCompiledPlugin.hpp"
#include "compiled/CompiledPluginPresentation.hpp"
#include "core/DeviceInfo.hpp"

namespace magda::daw::ui {

/**
 * @brief Inline editor panel for the Pitch shifter.
 *
 * Horizontal semitone ruler from -24 to +24 with octave ticks. Engine
 * shapes the marker(s):
 *  - Shifter: one bright dot at pitch+fine.
 *  - Detuner: two dots, +shift labelled L and -shift labelled R.
 *  - Harmonizer: a dimmed dot at 0 (dry) and a bright dot at the harmony
 *    interval.
 *
 * Read-only (mirrors Dimension); user edits go through the param grid.
 */
class CompiledPitchEditorView final : public juce::Component,
                                      public CompiledDevicePanel,
                                      private juce::Timer {
  public:
    explicit CompiledPitchEditorView(juce::String pluginId);

    int getPreferredHeight() const {
        return 56;
    }

    void setCompiledPlugin(magda::daw::audio::compiled::MagdaPitchCompiledPlugin* plugin);
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
    using Plugin = magda::daw::audio::compiled::MagdaPitchCompiledPlugin;

    void timerCallback() override;
    void resampleFromDevice();

    magda::daw::audio::compiled::MagdaPitchCompiledPlugin* compiledPlugin_ = nullptr;
    magda::DeviceInfo deviceSnapshot_;

    int engine_ = 0;
    float pitchSemis_ = 0.0f;
    float fineCents_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledPitchEditorView)
};

}  // namespace magda::daw::ui
