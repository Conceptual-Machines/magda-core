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
 * @brief Custom faceplate for Materia (magda_elements), the modal-synthesis
 *        voice ported from Mutable Instruments Elements.
 *
 * Layout follows the design mockup:
 *
 *   VOICE     Contour | Signature | Coarse | Fine | Level   (value boxes)
 *   EXCITER   [blend pad placeholder]  Bow / Blow / Strike columns
 *   RESONATOR [modal-response placeholder]  Geometry/Brightness/Damping/Position/Space
 *
 * Every control is a LinkableTextSlider carrying its param index via
 * setParamIndex(), so mod/macro/automation/MIDI-Learn linking is wired by the
 * standard DeviceSlotComponent::setupCustomUILinking() path. The manager pushes
 * live values via updateFromParameters().
 *
 * The excitation-blend triangle, modal-response spectrum and level meter are
 * drawn as placeholders in this pass; the live viz + meter need an audio tap
 * (tracked separately).
 */
class MateriaUI : public juce::Component {
  public:
    MateriaUI();
    ~MateriaUI() override;

    void updateFromParameters(const std::vector<magda::ParameterInfo>& params);
    std::vector<LinkableTextSlider*> getLinkableSliders();

    std::function<void(int paramIndex, float value)> onParameterChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    // Param indices — must match MutableElementsPlugin::ParamIndex.
    enum Param {
        kContour = 0,
        kBow,
        kBowTimbre,
        kBlow,
        kBlowFlow,
        kBlowTimbre,
        kStrike,
        kStrikeMallet,
        kStrikeTimbre,
        kSignature,
        kGeometry,
        kBrightness,
        kDamping,
        kPosition,
        kSpace,
        kPitch,
        kFine,
        kLevel,
        kNumParams
    };

    struct Control {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<LinkableTextSlider> slider;
    };

    // Lay a vertical stack of [label + slider] rows into `area`.
    void layoutRows(juce::Rectangle<int> area, const std::vector<int>& indices, int rowH);

    std::array<Control, kNumParams> controls_;

    // Section rectangles, cached in resized() for painted titles/placeholders.
    juce::Rectangle<int> voiceArea_, exciterArea_, resonatorArea_;
    juce::Rectangle<int> blendArea_, bowCol_, blowCol_, strikeCol_;
    juce::Rectangle<int> modalVizArea_, meterArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MateriaUI)
};

}  // namespace magda::daw::ui
