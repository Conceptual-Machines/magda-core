#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "audio/plugins/compiled/MagdaBitcrusherCompiledPlugin.hpp"
#include "compiled/CompiledPluginPresentation.hpp"
#include "core/DeviceInfo.hpp"

namespace magda::daw::ui {

/**
 * @brief Inline editor for the Bitcrusher.
 *
 * Draws the quantization staircase (output vs input) for the current
 * Bits setting, with a Rate readout in the corner. Visual reads like a
 * scope of the transfer function: more bits = finer staircase, fewer
 * bits = chunkier steps.
 */
class CompiledBitcrusherEditorView final : public juce::Component,
                                           public CompiledDevicePanel,
                                           private juce::Timer {
  public:
    explicit CompiledBitcrusherEditorView(juce::String pluginId);

    int getPreferredHeight() const {
        return 56;
    }

    void setCompiledPlugin(magda::daw::audio::compiled::MagdaBitcrusherCompiledPlugin* plugin);
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
    using Plugin = magda::daw::audio::compiled::MagdaBitcrusherCompiledPlugin;

    void timerCallback() override;
    void resampleFromDevice();

    magda::daw::audio::compiled::MagdaBitcrusherCompiledPlugin* compiledPlugin_ = nullptr;
    magda::DeviceInfo deviceSnapshot_;

    float bits_ = 8.0f;
    float rateHz_ = 8000.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledBitcrusherEditorView)
};

}  // namespace magda::daw::ui
