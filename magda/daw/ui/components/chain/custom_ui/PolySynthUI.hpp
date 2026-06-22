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
 * @brief Single-page custom UI for the compiled Poly Synth (magda_polysynth).
 *
 * Lays the 28 host slots out as four labelled segments on one page:
 *
 *     OSC        FILTER
 *     AMP ADSR   FILTER ADSR
 *
 * Every control is a LinkableTextSlider carrying its host slot index via
 * setParamIndex(), so mod / macro / automation / MIDI-Learn drag-linking is
 * wired by the standard DeviceSlotComponent::setupCustomUILinking() path
 * (exactly like FourOscUI / FaustInstrumentTabbedUI). The manager pushes live
 * values in via updateFromParameters().
 */
class PolySynthUI : public juce::Component {
  public:
    PolySynthUI();
    ~PolySynthUI() override = default;

    /// Push current parameter values (and ranges) into the matching sliders.
    void updateFromParameters(const std::vector<magda::ParameterInfo>& params);

    /// Flat list of every slider (host slot index carried via setParamIndex).
    /// Consumed by DeviceSlotComponent::setupCustomUILinking().
    std::vector<LinkableTextSlider*> getLinkableSliders();

    std::function<void(int paramIndex, float value)> onParameterChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    // Host slot layout — must match magda_polysynth.dsp / the C++ wrapper.
    static constexpr int kNumOscillators = 4;
    static constexpr int kOscSlotCount = 4;  // wave / level / coarse / fine
    static constexpr int kNumParams = 28;

    static constexpr int kFilterTypeSlot = 16;
    static constexpr int kCutoffSlot = 17;
    static constexpr int kResonanceSlot = 18;
    static constexpr int kFilterEnvAmtSlot = 19;
    static constexpr int kFilterAttackSlot = 20;  // .. 23 (D/S/R)
    static constexpr int kAmpAttackSlot = 24;     // .. 27 (D/S/R)

    struct Control {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<LinkableTextSlider> slider;
    };

    // Lay `indices` out as a label-on-top grid inside `area` (after reserving
    // the section title strip), `cols` columns wide.
    void layoutSection(juce::Rectangle<int> area, const std::vector<int>& indices, int cols);

    std::array<Control, kNumParams> controls_;
    std::array<juce::String, kNumParams> labels_;

    // Cached section rectangles for the painted titles.
    juce::Rectangle<int> oscArea_, filterArea_, ampArea_, filterEnvArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PolySynthUI)
};

}  // namespace magda::daw::ui
