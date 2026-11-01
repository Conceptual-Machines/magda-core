// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "compiled/CompiledFilterCurveView.hpp"
#include "core/ParameterInfo.hpp"
#include "custom_ui/AdsrGraph.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"
#include "ui/components/common/SegmentedChoice.hpp"

namespace magda::daw::audio::compiled {
class MagdaPolySynthCompiledPlugin;
}

namespace magda::daw::ui {

/**
 * @brief Custom UI for the compiled Poly Synth (magda_polysynth), in the v1 faceplate style.
 *
 * Three panels: OSC (a row per oscillator) and VOICE (unison, the performance controls and the
 * output) as tabs (#3201), the two filters as tabs with their routing, and the amp and filter
 * envelopes as tabs. The sidebar can hide the first panel. Values
 * sit in text tiles; every slot keeps a LinkableTextSlider, hidden where a segmented
 * choice, combo or toggle shows it, so linking reaches all of them.
 */
class PolySynthUI : public juce::Component, private juce::Timer {
  public:
    PolySynthUI();
    ~PolySynthUI() override;

    /// Push current parameter values (and ranges) into the matching sliders.
    void updateFromParameters(const std::vector<magda::ParameterInfo>& params);

    /// Flat list of every slider (host slot index carried via setParamIndex).
    /// Consumed by DeviceSlotComponent::setupCustomUILinking().
    std::vector<LinkableTextSlider*> getLinkableSliders();

    /// Bind the live plugin so the filter response follows effective
    /// (automation/modulation-inclusive) parameter values.
    void setLivePlugin(magda::daw::audio::compiled::MagdaPolySynthCompiledPlugin* plugin);

    std::function<void(int paramIndex, float value)> onParameterChanged;

    /// Show or hide the OSC panel; the device's width follows preferredWidth() (#3201).
    void setOscPanelShown(bool shown);
    int preferredWidth() const;

    void lookAndFeelChanged() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

  private:
    // Host slot layout — must match magda_polysynth.dsp / the C++ wrapper.
    static constexpr int kNumOscillators = 4;
    static constexpr int kOscSlotCount = 4;  // wave / level / coarse / fine
    static constexpr int kNumParams = 57;

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
    static constexpr int kFilterRoutingSlot = 52;
    static constexpr int kFilterBlendSlot = 53;
    static constexpr int kUnisonVoicesSlot = 54;
    static constexpr int kUnisonDetuneSlot = 55;
    static constexpr int kUnisonSpreadSlot = 56;
    static constexpr int kNumFilters = 2;

    struct FilterSlots {
        int type, cutoff, resonance, envAmt, drive, poles, enable;
    };
    static constexpr std::array<FilterSlots, kNumFilters> kFilterSlots{
        {{kFilterTypeSlot, kCutoffSlot, kResonanceSlot, kFilterEnvAmtSlot, kFilterDriveSlot,
          kFilterSlopeSlot, 50},
         {44, 45, 46, 47, 48, 49, 51}}};

    /// What the response curve draws for one filter.
    struct CurveState {
        int type = 0;
        float cutoffHz = 3000.0f;
        float resonance = 0.3f;
        float drive = 0.0f;
        int poles = 0;
        bool operator==(const CurveState&) const = default;
    };

    struct Control {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<LinkableTextSlider> slider;
        juce::Rectangle<int> tile;  ///< Painted behind the caption and value; empty when unplaced.
    };

    // A tile at @p bounds holding slot @p slot's caption and value.
    void placeTile(int slot, juce::Rectangle<int> bounds);
    // Lay @p slots out as tiles across @p area, @p cols wide.
    void placeTiles(juce::Rectangle<int> area, const std::vector<int>& slots, int cols);

    // Push a value-box edit into the envelope graph that owns that slot.
    void syncGraphFromParam(int paramIndex, float value);
    // Cache a filter, oscillator-toggle or routing value and refresh what shows it.
    void syncChoiceFromParam(int paramIndex, float value);
    // Write a choice slot from its control.
    void setChoiceSlot(int slot, int value);
    void refreshOscRows();
    // Show the selected OSC/VOICE tab's controls.
    void refreshOscPanel();
    // Show the selected filter's controls and reflect its choices.
    void refreshFilterPanel();
    void refreshEnvelopePanel();
    void pushFilterCurve();
    void pushFilterCurve(const CurveState& state);
    void timerCallback() override;
    void styleControls();

    void layoutOscPanel();
    void layoutFilterPanel();
    void layoutEnvelopePanel();

    std::array<Control, kNumParams> controls_;

    std::unique_ptr<AdsrGraph> ampGraph_;
    std::unique_ptr<AdsrGraph> filterGraph_;

    // Shared filter response graph (the exact component the compiled Faust
    // filter device uses), driven from the selected filter's params.
    std::unique_ptr<CompiledFilterCurveView> filterCurve_;
    magda::daw::audio::compiled::MagdaPolySynthCompiledPlugin* livePlugin_ = nullptr;

    // Last effective values read from the live AutomatableParameters. Stored
    // separately from the base-value cache below so a regular model refresh
    // cannot briefly pull a modulated curve back to its unmodulated position.
    std::optional<CurveState> liveCurve_;
    bool liveCurveRefreshPending_ = false;

    // Cached base values (dsp defaults) per filter.
    std::array<CurveState, kNumFilters> filterState_{};
    std::array<bool, kNumFilters> filterEnabled_{true, false};
    int filterRouting_ = 0;  // 0 = Serial, 1 = Parallel
    int selectedFilter_ = 0;
    bool showFilterEnvelope_ = false;
    bool showVoiceTab_ = false;
    bool oscShown_ = true;
    bool showsOsc() const {
        return oscShown_ && !showVoiceTab_;
    }
    bool showsVoice() const {
        return oscShown_ && showVoiceTab_;
    }
    std::array<bool, kNumOscillators> oscReset_{};
    std::array<bool, kNumOscillators> oscEnabled_{true, true, true, true};

    std::array<int, kNumOscillators> oscWave_{};
    std::array<std::unique_ptr<juce::Drawable>, 4> waveIcons_;
    void showWaveMenu(int osc);
    magda::SegmentedChoice routingChoice_, typeChoice_, polesChoice_, voicesChoice_,
        voiceModeChoice_;

    // Painted parts, kept for hit-testing.
    juce::Rectangle<int> oscPanel_, filterPanel_, envelopePanel_, voicesTile_, voicesCaption_;
    bool voicesCaptionOnTop_ = false;
    std::array<juce::Rectangle<int>, kNumOscillators> waveTiles_, waveIconAreas_, enableDots_,
        resetTiles_;
    std::array<juce::Rectangle<int>, kNumFilters> filterTabs_, filterDots_;
    std::array<juce::Rectangle<int>, 2> envelopeTabs_, oscTabs_;
    void layoutVoicePanel();
    /// LVL, CRS, FINE and RST over the oscillator columns, in a row under the tabs.
    std::array<juce::Rectangle<int>, kOscSlotCount> oscColumnHeads_;
    juce::Rectangle<int> polesCaption_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PolySynthUI)
};

}  // namespace magda::daw::ui
