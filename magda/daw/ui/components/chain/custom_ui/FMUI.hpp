// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "compiled/CompiledFilterCurveView.hpp"
#include "core/ParameterInfo.hpp"
#include "custom_ui/AdsrGraph.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"
#include "ui/components/common/SegmentedChoice.hpp"

namespace magda::daw::ui {

/**
 * @brief Custom UI for the compiled FM synth (magda_fm), in the v1 faceplate style.
 *
 * Three panels: OPS (wave, ratio, level and reset per operator, and the filter's level), AMP ENV
 * and VOICE as tabs; the MATRIX; and the FILTER and the FILTER ENV as
 * tabs (#3201). The sidebar can hide the first two. Every slot keeps a LinkableTextSlider, hidden
 * where a segmented choice, combo or toggle shows it, so linking reaches all of them.
 */
class FMUI : public juce::Component {
  public:
    FMUI();
    ~FMUI() override;

    void updateFromParameters(const std::vector<magda::ParameterInfo>& params);
    std::vector<LinkableTextSlider*> getLinkableSliders();

    std::function<void(int paramIndex, float value)> onParameterChanged;

    void lookAndFeelChanged() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    /// Show or hide the OPS and MATRIX panels; the device's width follows preferredWidth().
    void setPanelsShown(bool opsShown, bool matrixShown);
    int preferredWidth() const;

  private:
    // Host slot layout - must match magda_fm.dsp / the C++ wrapper.
    static constexpr int kNumOps = 4;
    static constexpr int kNodes = kNumOps + 1;  // the operators and the filter
    static constexpr int kMatrixBase = 0;       // 0..15, idx = src*4 + dst
    static constexpr int kRatioBase = 16;
    static constexpr int kLevelBase = 20;
    static constexpr int kAmpAdsrBase = 24;
    static constexpr int kWaveBase = 28;
    static constexpr int kGlideSlot = 32;
    static constexpr int kVelAmtSlot = 33;
    static constexpr int kResetBase = 34;
    static constexpr int kEnableBase = 38;
    static constexpr int kGainSlot = 42;
    static constexpr int kVoiceModeSlot = 43;
    static constexpr int kFilterEnableSlot = 44;
    static constexpr int kFilterEngineSlot = 45;
    static constexpr int kFilterTypeSlot = 46;
    static constexpr int kFilterCombModeSlot = 47;
    static constexpr int kFilterPolesSlot = 48;
    static constexpr int kFilterCutoffSlot = 49;
    static constexpr int kFilterResonanceSlot = 50;
    static constexpr int kFilterDriveSlot = 51;
    static constexpr int kFilterKeyTrackSlot = 52;
    static constexpr int kFilterVelSlot = 53;
    static constexpr int kFilterEnvSlot = 54;
    static constexpr int kFilterAdsrBase = 55;
    static constexpr int kFilterSendBase = 59;    // 59..62, Op n > F
    static constexpr int kFilterReturnBase = 63;  // 63..66, F > Op n
    static constexpr int kFilterLevelSlot = 67;
    static constexpr int kFilterPositionSlot = 68;
    static constexpr int kBendRangeSlot = 69;
    static constexpr int kNumParams = 70;

    struct Control {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<LinkableTextSlider> slider;
        juce::Rectangle<int> tile;  ///< Painted behind the value; empty when unplaced.
    };

    /// What the FILTER panel and the matrix's F cells show, cached from the slots.
    struct FilterState {
        bool on = false;
        bool inMatrix = false;
        int engine = 0;  // SVF / Ladder / Comb
        int type = 0;    // LP / HP / BP / Notch
        int combMode = 0;
        int poles = 0;
        float cutoffHz = 2000.0f;
        float resonance = 0.3f;
        float drive = 0.0f;
    };

    /// The slot in matrix cell (@p src, @p dst), the filter being node 4; -1 for F > F.
    static int matrixSlot(int src, int dst);

    // A tile at @p bounds: slot @p slot's caption above its value, or the value alone.
    void placeTile(int slot, juce::Rectangle<int> bounds, bool withCaption = true);
    void placeTiles(juce::Rectangle<int> area, const std::vector<int>& slots, int cols);
    void layoutFirstPanel();
    void layoutOperators(juce::Rectangle<int> area);
    void layoutVoice(juce::Rectangle<int> area);
    void layoutMatrix();
    void layoutFilterPanel();
    void layoutEnvelope(juce::Rectangle<int> area, AdsrGraph& graph, int adsrBase);
    void styleControls();

    // Write a choice slot from its segment, combo or toggle.
    void setChoiceSlot(int slot, int value);
    // Cache a choice or filter value and refresh what shows it.
    void syncFromParam(int slot, float value);
    void syncGraphFromParam(int slot, float value);
    void refreshOperatorRows();
    // Show the selected tabs' controls.
    void refreshFirstPanel();
    void refreshMatrix();
    void refreshFilterPanel();
    // Show or hide a placed slot's value, and its caption with @p withLabel.
    void showSlot(int slot, bool shown, bool withLabel);

    std::array<Control, kNumParams> controls_;

    /// Each operator's wave, drawn as its icon; a click picks another from a menu (#3201).
    std::array<int, kNumOps> opWave_{};
    std::array<std::unique_ptr<juce::Drawable>, 5> waveIcons_;
    std::array<juce::Rectangle<int>, kNumOps> waveIconAreas_;
    void showWaveMenu(int op);
    std::array<bool, kNumOps> opEnabled_{true, true, true, true};
    std::array<bool, kNumOps> opReset_{};

    std::unique_ptr<AdsrGraph> ampGraph_;
    std::unique_ptr<AdsrGraph> filterGraph_;
    enum class FirstTab { Ops, AmpEnv, Voice };
    FirstTab firstTab_ = FirstTab::Ops;
    /// Whether the first panel is shown on @p tab.
    bool shows(FirstTab tab) const {
        return opsShown_ && firstTab_ == tab;
    }
    bool showFilterEnvelope_ = false;
    bool opsShown_ = true;
    bool matrixShown_ = true;

    FilterState filter_;
    std::unique_ptr<CompiledFilterCurveView> filterCurve_;
    magda::SegmentedChoice positionChoice_, engineChoice_, typeChoice_, combChoice_, polesChoice_,
        voiceModeChoice_;

    // Painted parts, kept for paint and hit-testing.
    juce::Rectangle<int> firstPanel_, matrixPanel_, filterPanel_;
    std::array<juce::Rectangle<int>, kNumOps> waveTiles_, enableDots_, resetTiles_;
    std::array<juce::Rectangle<int>, kNodes> matrixColLabels_, matrixRowLabels_;
    /// WAVE, RATIO, LEVEL and RST over the operator columns.
    std::array<juce::Rectangle<int>, 4> opColumnCaptions_;
    juce::Rectangle<int> filterRowTile_, filterDot_, sendsCaption_;
    /// OPS | AMP ENV | VOICE, and FILTER | FILTER ENV; the FILTER tab carries the enable dot.
    std::array<juce::Rectangle<int>, 3> firstTabs_;
    std::array<juce::Rectangle<int>, 2> filterTabs_;
    juce::Rectangle<int> voiceModeTile_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FMUI)
};

}  // namespace magda::daw::ui
