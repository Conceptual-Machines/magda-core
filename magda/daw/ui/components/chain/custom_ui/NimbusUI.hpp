#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "core/ParameterInfo.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"

namespace magda::daw::audio {
class MutableCloudsPlugin;
}

namespace magda::daw::ui {

/**
 * @brief Custom faceplate for Nimbus (magda_clouds), the granular texture
 *        processor ported from Mutable Instruments Clouds.
 *
 * Layout follows the design mockup:
 *
 *   GRAIN BUFFER  [animated record-buffer view with the grain window + cloud]
 *   PARAMETERS    Position Size Pitch / Density Texture          (value boxes)
 *   <controls>    FREEZE toggle, PLAYBACK MODE (4), BLEND ROUTES (4 values)
 *
 * The grain-buffer view is decorative for this first iteration (no signal-in
 * tap yet): a synthetic waveform with the grain window driven by Position/Size
 * and an animated grain cloud driven by Density/Texture. Continuous controls
 * are LinkableTextSliders; Mode and Freeze are drawn as clickable segments.
 */
class NimbusUI : public juce::Component, private juce::Timer {
  public:
    NimbusUI();
    ~NimbusUI() override;

    void updateFromParameters(const std::vector<magda::ParameterInfo>& params);
    std::vector<LinkableTextSlider*> getLinkableSliders();

    // Bind the live plugin so the grain-buffer view shows the real input.
    void setPlugin(daw::audio::MutableCloudsPlugin* plugin);

    std::function<void(int paramIndex, float value)> onParameterChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

  private:
    // Param indices — must match MutableCloudsPlugin::ParamIndex.
    enum Param {
        kPosition = 0,
        kSize,
        kPitch,
        kDensity,
        kTexture,
        kDryWet,
        kSpread,
        kFeedback,
        kReverb,
        kMode,
        kFreeze,
        kNumParams
    };

    static constexpr int kNumModes = 4;

    struct Control {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<LinkableTextSlider> slider;
    };

    void timerCallback() override;
    void layoutRow(juce::Rectangle<int> row, const std::vector<int>& indices);
    void paintBuffer(juce::Graphics& g);

    std::array<Control, kNumParams> controls_;

    daw::audio::MutableCloudsPlugin* plugin_ = nullptr;
    int curMode_ = 0;
    bool freeze_ = false;
    float animPhase_ = 0.0f;

    juce::Rectangle<int> grainArea_, bufferVizArea_, paramsArea_, ctrlArea_;
    juce::Rectangle<int> freezeBtn_, modeLabelRect_, blendLabelRect_;
    std::array<juce::Rectangle<int>, kNumModes> modeBtn_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NimbusUI)
};

}  // namespace magda::daw::ui
