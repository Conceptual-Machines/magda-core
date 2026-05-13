#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "compiled/CompiledPluginPresentation.hpp"
#include "core/DeviceInfo.hpp"

namespace magda::daw::audio::compiled {
class MagdaSaturatorCompiledPlugin;
}

namespace magda::daw::ui {

/**
 * @brief Inline transfer-curve display for the compiled saturator.
 *
 * Plots y = soft_limit(output_lin · nl(mode, drive_lin · x + bias)) over
 * x ∈ [-1, 1] for the live host-param values. Mirrors the chain the DSP
 * actually runs (see magda_saturator.dsp) so what the user sees is what
 * the audio path does.
 *
 * Unlike the filter curve, the transfer function is stateless — no
 * smoothing or block-rate animation needed. Polls the host params at
 * ~30 Hz and repaints when any of Drive / Mode / Bias / Output / Mix
 * crosses an epsilon. Mode also forces a repaint because changing
 * waveshape redraws the whole curve.
 */
class CompiledSaturatorCurveView final : public juce::Component,
                                         public CompiledDevicePanel,
                                         private juce::Timer {
  public:
    explicit CompiledSaturatorCurveView(juce::String pluginId);

    int getPreferredHeight() const {
        return 140;
    }

    void setCompiledPlugin(magda::daw::audio::compiled::MagdaSaturatorCompiledPlugin* plugin);
    void updateFromDevice(const magda::DeviceInfo& device) override;

    // CompiledDevicePanel
    juce::Component& component() override {
        return *this;
    }
    void bindPlugin(te::Plugin* plugin) override;
    void setOnParameterChanged(std::function<void(int, float)>) override {}  // read-only view
    int preferredHeight() const override {
        return getPreferredHeight();
    }

    void paint(juce::Graphics& g) override;

  private:
    void timerCallback() override;
    void resampleFromPlugin();

    // Stateless waveshape — same six branches the DSP runs.
    enum class Mode { Tanh, Soft, Hard, Fold, Tube, Tape };
    static float shapeSample(Mode mode, float x);

    magda::daw::audio::compiled::MagdaSaturatorCompiledPlugin* compiledPlugin_ = nullptr;
    magda::DeviceInfo deviceSnapshot_;

    // Live display values — the curve is purely a function of these. We
    // only repaint when one of them moves materially, so a static curve
    // costs basically nothing per timer tick.
    float driveDb_ = 0.0f;
    float bias_ = 0.0f;
    float outputDb_ = 0.0f;
    float mix_ = 1.0f;
    int modeIndex_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledSaturatorCurveView)
};

}  // namespace magda::daw::ui
