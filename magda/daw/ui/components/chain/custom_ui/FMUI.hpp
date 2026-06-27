#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "core/ParameterInfo.hpp"
#include "custom_ui/AdsrGraph.hpp"
#include "ui/components/common/IconSelector.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief Custom UI for the compiled FM synth (magda_fm).
 *
 * Centrepiece is the 4x4 modulation matrix: a grid of value boxes where cell
 * (row=src, col=dst) is M(src,dst) - how much operator src phase-modulates
 * operator dst (the diagonal is self-feedback). Below it, one column per
 * operator (Wave dropdown / Ratio / Level) and a row for the amp ADSR + Gain.
 *
 * Every control is a LinkableTextSlider carrying its host slot index via
 * setParamIndex(), so mod / macro / automation / MIDI-Learn drag-linking is
 * wired by DeviceSlotComponent::setupCustomUILinking(); the manager pushes live
 * values in via updateFromParameters(). Wave dropdowns overlay hidden sliders
 * (which keep the value + linking), the same trick PolySynthUI uses.
 */
class FMUI : public juce::Component {
  public:
    FMUI();
    ~FMUI() override;

    void updateFromParameters(const std::vector<magda::ParameterInfo>& params);
    std::vector<LinkableTextSlider*> getLinkableSliders();

    std::function<void(int paramIndex, float value)> onParameterChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    // Host slot layout - must match magda_fm.dsp / the C++ wrapper.
    static constexpr int kNumOps = 4;
    static constexpr int kMatrixBase = 0;    // 0..15, idx = src*4 + dst
    static constexpr int kRatioBase = 16;    // 16..19
    static constexpr int kLevelBase = 20;    // 20..23
    static constexpr int kAmpAdsrBase = 24;  // 24..27 (A/D/S/R)
    static constexpr int kWaveBase = 28;     // 28..31
    static constexpr int kGlideSlot = 32;
    static constexpr int kVelAmtSlot = 33;
    static constexpr int kResetBase = 34;   // 34..37 (per-op phase reset)
    static constexpr int kEnableBase = 38;  // 38..41 (per-op enable / mute)
    static constexpr int kGainSlot = 42;
    static constexpr int kVoiceModeSlot = 43;
    static constexpr int kNumParams = 44;
    static constexpr int kNumWaves = 5;  // Sine / Triangle / Saw / Square / Noise

    struct Control {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<LinkableTextSlider> slider;
    };

    void setOpWave(int op, int wave);
    void updateWaveSelectors();
    void setOpReset(int op, bool on);
    void updateResetButtons();
    void setOpEnable(int op, bool on);
    void updateEnableButtons();
    // Grey out (and disable) every control in a disabled operator's column.
    void applyOpColumnEnabled(int op);
    void layoutCells(juce::Rectangle<int> area, const std::vector<int>& indices, int cols);

    std::array<Control, kNumParams> controls_;

    // Per-operator waveform icon selectors (overlay the hidden wave sliders).
    std::array<IconSelector, kNumOps> waveSelectors_;
    // Per-operator phase-reset toggles (overlay the hidden reset sliders).
    std::array<std::unique_ptr<juce::TextButton>, kNumOps> resetButtons_;
    std::array<bool, kNumOps> opReset_{};
    // Per-operator enable (mute) toggles; disabling greys out the column.
    std::array<std::unique_ptr<juce::TextButton>, kNumOps> enableButtons_;
    std::array<bool, kNumOps> opEnabled_{};  // all true after construction

    // Draggable amp ADSR envelope (replaces the A/D/S/R value boxes as the editor).
    std::unique_ptr<AdsrGraph> ampGraph_;

    // Cached section rectangles for painted titles / matrix headers.
    juce::Rectangle<int> matrixArea_, opsArea_, ampArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FMUI)
};

}  // namespace magda::daw::ui
