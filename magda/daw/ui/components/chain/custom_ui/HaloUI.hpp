#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "core/ParameterInfo.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief Custom faceplate for Halo (magda_rings), the resonator ported from
 *        Mutable Instruments Rings.
 *
 * Layout follows the design mockup:
 *
 *   RESONATOR     [modal-response spectrum]            modal / partials / poly
 *   PARAMETERS    Structure Brightness Damping Position / Chord Pitch Fine Level
 *   RES. MODEL    six model buttons + polyphony (1 / 2 / 4 voices)
 *
 * The continuous controls are LinkableTextSliders carrying their param index, so
 * mod/macro/automation/MIDI-Learn linking is wired by the standard
 * DeviceSlotComponent path. Model and polyphony are discrete and drawn as
 * clickable segments that write their param directly via onParameterChanged.
 */
class HaloUI : public juce::Component {
  public:
    HaloUI();
    ~HaloUI() override;

    void updateFromParameters(const std::vector<magda::ParameterInfo>& params);
    std::vector<LinkableTextSlider*> getLinkableSliders();

    std::function<void(int paramIndex, float value)> onParameterChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

  private:
    // Param indices — must match MutableRingsPlugin::ParamIndex.
    enum Param {
        kStructure = 0,
        kBrightness,
        kDamping,
        kPosition,
        kModel,
        kPolyphony,
        kChord,
        kPitch,
        kFine,
        kLevel,
        kNumParams
    };

    static constexpr int kNumModels = 6;
    static constexpr int kNumPoly = 3;  // 1 / 2 / 4 voices

    struct Control {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<LinkableTextSlider> slider;
    };

    void layoutRow(juce::Rectangle<int> row, const std::vector<int>& indices);

    std::array<Control, kNumParams> controls_;

    // Cached discrete-param values (model/polyphony are not sliders).
    int curModel_ = 0;
    int curPoly_ = 1;

    // Section rectangles, cached in resized() for painted titles/placeholders.
    juce::Rectangle<int> resonatorArea_, paramsArea_, modelArea_;
    juce::Rectangle<int> modalVizArea_, polyLabelRect_;
    std::array<juce::Rectangle<int>, kNumModels> modelBtn_;
    std::array<juce::Rectangle<int>, kNumPoly> polyBtn_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HaloUI)
};

}  // namespace magda::daw::ui
