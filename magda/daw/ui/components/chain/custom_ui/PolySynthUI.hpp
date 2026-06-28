#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "compiled/CompiledFilterCurveView.hpp"
#include "core/ParameterInfo.hpp"
#include "custom_ui/AdsrGraph.hpp"
#include "ui/components/common/IconSelector.hpp"
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
    ~PolySynthUI() override;

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
    static constexpr int kNumParams = 44;

    static constexpr int kFilterTypeSlot = 16;
    static constexpr int kCutoffSlot = 17;
    static constexpr int kResonanceSlot = 18;
    static constexpr int kFilterEnvAmtSlot = 19;
    static constexpr int kFilterAttackSlot = 20;  // .. 23 (D/S/R)
    static constexpr int kAmpAttackSlot = 24;     // .. 27 (D/S/R)
    static constexpr int kFilterDriveSlot = 28;
    static constexpr int kFilterSlopeSlot = 29;
    static constexpr int kBendRangeSlot = 30;
    static constexpr int kVoiceModeSlot = 31;
    static constexpr int kGlideSlot = 32;
    static constexpr int kOscResetBaseSlot = 33;  // osc n -> + (n - 1), idx 33..36
    static constexpr int kVelAmpSlot = 37;
    static constexpr int kVelFilterSlot = 38;
    static constexpr int kOscEnableBaseSlot = 39;  // osc n -> + (n - 1), idx 39..42
    static constexpr int kOutputGainSlot = 43;     // master output gain (dB)
    static constexpr int kNumFilterTypes = 4;      // Lowpass / Highpass / Bandpass / Notch
    static constexpr int kNumSlopes = 2;           // 12 dB / 24 dB
    static constexpr int kNumVoiceModes = 3;       // Poly / Mono / Legato

    struct Control {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<LinkableTextSlider> slider;
    };

    // Lay `indices` out as a label-on-top grid inside `area` (after reserving
    // the section title strip), `cols` columns wide.
    void layoutSection(juce::Rectangle<int> area, const std::vector<int>& indices, int cols);
    // Like layoutSection but reserves the upper portion for `graph` (a draggable
    // envelope) and lays the value boxes in a single row beneath it.
    void layoutAdsrSection(juce::Rectangle<int> area, AdsrGraph* graph,
                           const std::vector<int>& indices, int cols = 2);
    // Label-on-top grid of `indices` filling `area` exactly (no title/gap).
    void layoutCells(juce::Rectangle<int> area, const std::vector<int>& indices, int cols);

    // Push a value-box edit into the envelope graph that owns that slot.
    void syncGraphFromParam(int paramIndex, float value);
    // Update the cached filter value for `paramIndex` (if it is a filter slot)
    // and refresh the response curve.
    void syncFilterCurveFromParam(int paramIndex, float value);
    // Push the cached filter values into the shared response curve.
    void pushFilterCurve();
    // Filter Type segmented buttons.
    void setFilterType(int type);  // user click: writes param + refreshes
    void updateTypeButtons();      // reflect filterType_ in the button states
    // Filter Slope (12/24 dB) segmented buttons.
    void setFilterSlope(int slope);
    void updateSlopeButtons();
    // Voice Mode (Poly / Mono / Legato) segmented buttons.
    void setVoiceMode(int mode);
    void updateVoiceModeButtons();
    // Per-oscillator phase-reset on/off toggles.
    void setOscReset(int osc, bool on);
    void updateOscResetButtons();
    // Per-oscillator enable (mute) toggles. Disabling greys out the column.
    void setOscEnable(int osc, bool on);
    void updateOscEnableButtons();
    // Apply the enabled/disabled visual state (grey-out) to every control in an
    // oscillator's column.
    void applyOscColumnEnabled(int osc);
    // Per-oscillator wave icon selectors.
    void setOscWave(int osc, int wave);
    void updateWaveSelectors();
    // OSC section layout (wave selectors + value boxes + enable/reset toggles).
    void layoutOscSection();

    std::array<Control, kNumParams> controls_;
    std::array<juce::String, kNumParams> labels_;

    std::unique_ptr<AdsrGraph> ampGraph_;
    std::unique_ptr<AdsrGraph> filterGraph_;

    // Shared filter response graph (the exact component the compiled Faust
    // filter device uses), driven from the synth's SVF filter params.
    std::unique_ptr<CompiledFilterCurveView> filterCurve_;

    // Cached filter values (dsp defaults) used to drive the response curve.
    int filterType_ = 0;
    float filterCutoffHz_ = 3000.0f;
    float filterRes_ = 0.3f;
    float filterDrive_ = 0.0f;
    int filterSlope_ = 0;  // 0 = 12 dB, 1 = 24 dB
    int voiceMode_ = 0;    // 0 = Poly, 1 = Mono, 2 = Legato
    std::array<bool, kNumOscillators> oscReset_{};
    std::array<bool, kNumOscillators> oscEnabled_{};  // all true after construction

    // Segmented Filter Type + Slope + Voice Mode buttons (replace value boxes).
    std::array<std::unique_ptr<juce::TextButton>, kNumFilterTypes> typeButtons_;
    std::array<std::unique_ptr<juce::TextButton>, kNumSlopes> slopeButtons_;
    std::array<std::unique_ptr<juce::TextButton>, kNumVoiceModes> voiceModeButtons_;
    // Per-oscillator waveform icon selectors (replace the wave value boxes),
    // matching the FM operator UI.
    std::array<IconSelector, kNumOscillators> waveSelectors_;
    // Per-oscillator phase-reset toggles, one per oscillator column.
    std::array<std::unique_ptr<juce::TextButton>, kNumOscillators> oscResetButtons_;
    // Per-oscillator enable (mute) toggles, one per oscillator column.
    std::array<std::unique_ptr<juce::TextButton>, kNumOscillators> oscEnableButtons_;

    // Cached section rectangles for the painted titles.
    juce::Rectangle<int> oscArea_, filterArea_, ampArea_, filterEnvArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PolySynthUI)
};

}  // namespace magda::daw::ui
