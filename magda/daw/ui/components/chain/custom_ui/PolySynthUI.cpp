#include "custom_ui/PolySynthUI.hpp"

#include <algorithm>
#include <cmath>

#include "BinaryData.h"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

namespace {
constexpr int kSectionTitleH = 15;
constexpr int kCellLabelH = 12;
constexpr int kCellPad = 3;
constexpr int kSectionGap = 4;

// Waveform icons in the dsp's wave order: 0 Sine / 1 Saw / 2 Square / 3 Triangle,
// so the selector index maps 1:1 to the Osc Wave slot value.
void populateWaveSelector(IconSelector& sel) {
    sel.addOption(BinaryData::fadmodsine_svg, BinaryData::fadmodsine_svgSize, "Sine");
    sel.addOption(BinaryData::fadmodsawup_svg, BinaryData::fadmodsawup_svgSize, "Saw");
    sel.addOption(BinaryData::fadmodsquare_svg, BinaryData::fadmodsquare_svgSize, "Square");
    sel.addOption(BinaryData::fadmodtri_svg, BinaryData::fadmodtri_svgSize, "Triangle");
}
}  // namespace

PolySynthUI::PolySynthUI() {
    // Short per-slot labels. The oscillator cells are prefixed with the
    // oscillator number so each cell is self-describing in the dense grid.
    // Per-oscillator control labels. The column header (the enable toggle) carries
    // the oscillator number, so these are bare control names.
    for (int osc = 0; osc < kNumOscillators; ++osc) {
        const int base = osc * kOscSlotCount;
        labels_[static_cast<size_t>(base + 0)] = "Wave";
        labels_[static_cast<size_t>(base + 1)] = "Level";
        labels_[static_cast<size_t>(base + 2)] = "Coarse";
        labels_[static_cast<size_t>(base + 3)] = "Fine";
    }
    labels_[kFilterTypeSlot] = "Type";
    labels_[kCutoffSlot] = "Cutoff";
    labels_[kResonanceSlot] = "Reso";
    labels_[kFilterEnvAmtSlot] = "Env Amt";
    labels_[kFilterDriveSlot] = "Drive";
    labels_[kFilterSlopeSlot] = "Slope";
    labels_[kFilterAttackSlot + 0] = "Attack";
    labels_[kFilterAttackSlot + 1] = "Decay";
    labels_[kFilterAttackSlot + 2] = "Sustain";
    labels_[kFilterAttackSlot + 3] = "Release";
    labels_[kAmpAttackSlot + 0] = "Attack";
    labels_[kAmpAttackSlot + 1] = "Decay";
    labels_[kAmpAttackSlot + 2] = "Sustain";
    labels_[kAmpAttackSlot + 3] = "Release";
    labels_[kBendRangeSlot] = "Bend Range";
    labels_[kVoiceModeSlot] = "Mode";
    labels_[kGlideSlot] = "Glide";
    for (int osc = 0; osc < kNumOscillators; ++osc)
        labels_[kOscResetBaseSlot + osc] = "Rst";
    labels_[kVelAmpSlot] = "Vel>Amp";
    labels_[kVelFilterSlot] = "Vel>Cut";
    labels_[kOutputGainSlot] = "Output";

    for (int i = 0; i < kNumParams; ++i) {
        auto& c = controls_[static_cast<size_t>(i)];

        c.label = std::make_unique<juce::Label>();
        c.label->setText(labels_[static_cast<size_t>(i)], juce::dontSendNotification);
        c.label->setFont(FontManager::getInstance().getUIFont(10.0f));
        c.label->setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
        c.label->setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(*c.label);

        c.slider = std::make_unique<LinkableTextSlider>();
        c.slider->setParamIndex(i);
        const int idx = i;
        c.slider->onValueChanged = [this, idx](double v) {
            if (onParameterChanged)
                onParameterChanged(idx, static_cast<float>(v));
            syncGraphFromParam(idx, static_cast<float>(v));
            syncFilterCurveFromParam(idx, static_cast<float>(v));
        };
        addAndMakeVisible(*c.slider);
    }

    // Dragging an envelope handle writes the plugin value AND keeps the matching
    // value box in sync, so the boxes stay authoritative for linking/automation.
    auto wireGraph = [this](AdsrGraph& graph) {
        graph.onStageChanged = [this](int paramIndex, float value) {
            if (onParameterChanged)
                onParameterChanged(paramIndex, value);
            if (paramIndex >= 0 && paramIndex < kNumParams)
                controls_[static_cast<size_t>(paramIndex)].slider->setValue(
                    value, juce::dontSendNotification);
        };
    };
    ampGraph_ = std::make_unique<AdsrGraph>();
    filterGraph_ = std::make_unique<AdsrGraph>();
    wireGraph(*ampGraph_);
    wireGraph(*filterGraph_);
    addAndMakeVisible(*ampGraph_);
    addAndMakeVisible(*filterGraph_);

    filterCurve_ = std::make_unique<CompiledFilterCurveView>("magda_polysynth");
    filterCurve_->setCurveColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    addAndMakeVisible(*filterCurve_);
    pushFilterCurve();

    // Filter Type as a segmented multi-button row (Lowpass/Highpass/Bandpass/
    // Notch) instead of a value box. The hidden Type slider still carries the
    // value for state/linking; the buttons drive it.
    static const char* kTypeNames[kNumFilterTypes] = {"Lowpass", "Highpass", "Bandpass", "Notch"};
    for (int t = 0; t < kNumFilterTypes; ++t) {
        auto btn = std::make_unique<juce::TextButton>(kTypeNames[t]);
        btn->setLookAndFeel(&FlatTabButtonLookAndFeel::getInstance());  // theme font
        // Selection is driven explicitly via updateTypeButtons() (not the radio
        // group / click-toggle, which left two segments lit at once).
        btn->setClickingTogglesState(false);
        btn->setColour(juce::TextButton::buttonColourId,
                       DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.10f));
        btn->setColour(juce::TextButton::buttonOnColourId,
                       DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        btn->setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
        btn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        btn->setConnectedEdges((t > 0 ? juce::Button::ConnectedOnLeft : 0) |
                               (t < kNumFilterTypes - 1 ? juce::Button::ConnectedOnRight : 0));
        btn->onClick = [this, t]() { setFilterType(t); };
        addAndMakeVisible(*btn);
        typeButtons_[static_cast<size_t>(t)] = std::move(btn);
    }
    // Slope 12/24 dB segmented toggle (top-right of the filter section).
    static const char* kSlopeNames[kNumSlopes] = {"12 dB", "24 dB"};
    for (int s = 0; s < kNumSlopes; ++s) {
        auto btn = std::make_unique<juce::TextButton>(kSlopeNames[s]);
        btn->setLookAndFeel(&FlatTabButtonLookAndFeel::getInstance());
        btn->setClickingTogglesState(false);
        btn->setColour(juce::TextButton::buttonColourId,
                       DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.10f));
        btn->setColour(juce::TextButton::buttonOnColourId,
                       DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        btn->setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
        btn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        btn->setConnectedEdges(s == 0 ? juce::Button::ConnectedOnRight
                                      : juce::Button::ConnectedOnLeft);
        btn->onClick = [this, s]() { setFilterSlope(s); };
        addAndMakeVisible(*btn);
        slopeButtons_[static_cast<size_t>(s)] = std::move(btn);
    }

    // Voice Mode (Poly / Mono / Legato) segmented toggle in the global strip.
    static const char* kVoiceModeNames[kNumVoiceModes] = {"Poly", "Mono", "Legato"};
    for (int v = 0; v < kNumVoiceModes; ++v) {
        auto btn = std::make_unique<juce::TextButton>(kVoiceModeNames[v]);
        btn->setLookAndFeel(&FlatTabButtonLookAndFeel::getInstance());
        btn->setClickingTogglesState(false);
        btn->setColour(juce::TextButton::buttonColourId,
                       DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.10f));
        btn->setColour(juce::TextButton::buttonOnColourId,
                       DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        btn->setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
        btn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        btn->setConnectedEdges((v > 0 ? juce::Button::ConnectedOnLeft : 0) |
                               (v < kNumVoiceModes - 1 ? juce::Button::ConnectedOnRight : 0));
        btn->onClick = [this, v]() { setVoiceMode(v); };
        addAndMakeVisible(*btn);
        voiceModeButtons_[static_cast<size_t>(v)] = std::move(btn);
    }

    // Per-oscillator waveform icon selector (replaces the wave value box) plus an
    // enable toggle and a phase-reset toggle, one of each per oscillator column.
    oscEnabled_.fill(true);
    for (int osc = 0; osc < kNumOscillators; ++osc) {
        auto& sel = waveSelectors_[static_cast<size_t>(osc)];
        populateWaveSelector(sel);
        sel.onChange = [this, osc](int wave) { setOscWave(osc, wave); };
        addAndMakeVisible(sel);

        // Enable (mute) toggle, doubling as the column header. Lit = audible.
        auto en = std::make_unique<juce::TextButton>("OSC " + juce::String(osc + 1));
        en->setLookAndFeel(&FlatTabButtonLookAndFeel::getInstance());
        en->setClickingTogglesState(false);
        en->setColour(juce::TextButton::buttonColourId,
                      DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.10f));
        en->setColour(juce::TextButton::buttonOnColourId,
                      DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        en->setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
        en->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        en->setTooltip("Enable / disable this oscillator");
        en->onClick = [this, osc]() { setOscEnable(osc, !oscEnabled_[static_cast<size_t>(osc)]); };
        addAndMakeVisible(*en);
        oscEnableButtons_[static_cast<size_t>(osc)] = std::move(en);

        auto rst = std::make_unique<juce::TextButton>("R");  // phase-reset toggle
        rst->setLookAndFeel(&FlatTabButtonLookAndFeel::getInstance());
        rst->setClickingTogglesState(false);
        rst->setColour(juce::TextButton::buttonColourId,
                       DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.10f));
        rst->setColour(juce::TextButton::buttonOnColourId,
                       DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        rst->setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
        rst->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        rst->setTooltip("Reset oscillator phase on note-on");
        rst->onClick = [this, osc]() { setOscReset(osc, !oscReset_[static_cast<size_t>(osc)]); };
        addAndMakeVisible(*rst);
        oscResetButtons_[static_cast<size_t>(osc)] = std::move(rst);
    }

    // The Type/Slope/Mode value boxes and the per-osc Wave/Reset boxes are
    // replaced by the buttons/dropdowns; keep the objects (for linking/value) but
    // hide them.
    controls_[kFilterTypeSlot].slider->setVisible(false);
    controls_[kFilterTypeSlot].label->setVisible(false);
    controls_[kFilterSlopeSlot].slider->setVisible(false);
    controls_[kFilterSlopeSlot].label->setVisible(false);
    controls_[kVoiceModeSlot].slider->setVisible(false);
    controls_[kVoiceModeSlot].label->setVisible(false);
    for (int osc = 0; osc < kNumOscillators; ++osc) {
        // Icon selector / enable + reset toggles replace the value boxes; hide the
        // underlying sliders and the wave/reset labels (the toggles are labelled).
        controls_[osc * kOscSlotCount].slider->setVisible(false);
        controls_[osc * kOscSlotCount].label->setVisible(false);
        controls_[kOscResetBaseSlot + osc].slider->setVisible(false);
        controls_[kOscResetBaseSlot + osc].label->setVisible(false);
        controls_[kOscEnableBaseSlot + osc].slider->setVisible(false);
        controls_[kOscEnableBaseSlot + osc].label->setVisible(false);
    }
    // Slightly smaller font on the osc value boxes so the unit suffix is not
    // cramped and the boxes read lighter.
    for (int osc = 0; osc < kNumOscillators; ++osc)
        for (int p = 1; p < kOscSlotCount; ++p)  // Level / Coarse / Fine
            controls_[osc * kOscSlotCount + p].slider->setFont(
                FontManager::getInstance().getUIFont(10.0f));
    updateTypeButtons();
    updateSlopeButtons();
    updateVoiceModeButtons();
    updateOscResetButtons();
    updateOscEnableButtons();
    updateWaveSelectors();
}

PolySynthUI::~PolySynthUI() {
    for (auto& btn : typeButtons_)
        if (btn)
            btn->setLookAndFeel(nullptr);
    for (auto& btn : slopeButtons_)
        if (btn)
            btn->setLookAndFeel(nullptr);
    for (auto& btn : voiceModeButtons_)
        if (btn)
            btn->setLookAndFeel(nullptr);
    for (auto& btn : oscResetButtons_)
        if (btn)
            btn->setLookAndFeel(nullptr);
    for (auto& btn : oscEnableButtons_)
        if (btn)
            btn->setLookAndFeel(nullptr);
}

void PolySynthUI::setFilterType(int type) {
    type = juce::jlimit(0, kNumFilterTypes - 1, type);
    filterType_ = type;
    controls_[kFilterTypeSlot].slider->setValue(type, juce::dontSendNotification);
    if (onParameterChanged)
        onParameterChanged(kFilterTypeSlot, static_cast<float>(type));
    updateTypeButtons();
    pushFilterCurve();
}

void PolySynthUI::updateTypeButtons() {
    const int t = juce::jlimit(0, kNumFilterTypes - 1, filterType_);
    for (int i = 0; i < kNumFilterTypes; ++i)
        if (typeButtons_[static_cast<size_t>(i)])
            typeButtons_[static_cast<size_t>(i)]->setToggleState(i == t,
                                                                 juce::dontSendNotification);
}

void PolySynthUI::setFilterSlope(int slope) {
    slope = juce::jlimit(0, kNumSlopes - 1, slope);
    filterSlope_ = slope;
    controls_[kFilterSlopeSlot].slider->setValue(slope, juce::dontSendNotification);
    if (onParameterChanged)
        onParameterChanged(kFilterSlopeSlot, static_cast<float>(slope));
    updateSlopeButtons();
    pushFilterCurve();
}

void PolySynthUI::updateSlopeButtons() {
    const int s = juce::jlimit(0, kNumSlopes - 1, filterSlope_);
    for (int i = 0; i < kNumSlopes; ++i)
        if (slopeButtons_[static_cast<size_t>(i)])
            slopeButtons_[static_cast<size_t>(i)]->setToggleState(i == s,
                                                                  juce::dontSendNotification);
}

void PolySynthUI::setVoiceMode(int mode) {
    mode = juce::jlimit(0, kNumVoiceModes - 1, mode);
    voiceMode_ = mode;
    controls_[kVoiceModeSlot].slider->setValue(mode, juce::dontSendNotification);
    if (onParameterChanged)
        onParameterChanged(kVoiceModeSlot, static_cast<float>(mode));
    updateVoiceModeButtons();
}

void PolySynthUI::updateVoiceModeButtons() {
    const int m = juce::jlimit(0, kNumVoiceModes - 1, voiceMode_);
    for (int i = 0; i < kNumVoiceModes; ++i)
        if (voiceModeButtons_[static_cast<size_t>(i)])
            voiceModeButtons_[static_cast<size_t>(i)]->setToggleState(i == m,
                                                                      juce::dontSendNotification);
}

void PolySynthUI::setOscReset(int osc, bool on) {
    if (osc < 0 || osc >= kNumOscillators)
        return;
    oscReset_[static_cast<size_t>(osc)] = on;
    const int slot = kOscResetBaseSlot + osc;
    controls_[slot].slider->setValue(on ? 1.0 : 0.0, juce::dontSendNotification);
    if (onParameterChanged)
        onParameterChanged(slot, on ? 1.0f : 0.0f);
    updateOscResetButtons();
}

void PolySynthUI::updateOscResetButtons() {
    for (int osc = 0; osc < kNumOscillators; ++osc)
        if (oscResetButtons_[static_cast<size_t>(osc)])
            oscResetButtons_[static_cast<size_t>(osc)]->setToggleState(
                oscReset_[static_cast<size_t>(osc)], juce::dontSendNotification);
}

void PolySynthUI::setOscEnable(int osc, bool on) {
    if (osc < 0 || osc >= kNumOscillators)
        return;
    oscEnabled_[static_cast<size_t>(osc)] = on;
    const int slot = kOscEnableBaseSlot + osc;
    controls_[slot].slider->setValue(on ? 1.0 : 0.0, juce::dontSendNotification);
    if (onParameterChanged)
        onParameterChanged(slot, on ? 1.0f : 0.0f);
    updateOscEnableButtons();
    applyOscColumnEnabled(osc);
}

void PolySynthUI::updateOscEnableButtons() {
    for (int osc = 0; osc < kNumOscillators; ++osc)
        if (oscEnableButtons_[static_cast<size_t>(osc)])
            oscEnableButtons_[static_cast<size_t>(osc)]->setToggleState(
                oscEnabled_[static_cast<size_t>(osc)], juce::dontSendNotification);
}

void PolySynthUI::applyOscColumnEnabled(int osc) {
    if (osc < 0 || osc >= kNumOscillators)
        return;
    const bool on = oscEnabled_[static_cast<size_t>(osc)];
    const float alpha = on ? 1.0f : 0.3f;
    // Grey out (and disable interaction with) everything in the column except the
    // enable toggle itself, which stays live so the column can be re-enabled.
    const int base = osc * kOscSlotCount;
    for (int p = 0; p < kOscSlotCount; ++p) {
        auto& c = controls_[static_cast<size_t>(base + p)];
        c.slider->setEnabled(on);
        c.slider->setAlpha(alpha);
        if (c.label)
            c.label->setAlpha(alpha);
    }
    auto& sel = waveSelectors_[static_cast<size_t>(osc)];
    sel.setEnabled(on);
    sel.setAlpha(alpha);
    if (auto& rst = oscResetButtons_[static_cast<size_t>(osc)]) {
        rst->setEnabled(on);
        rst->setAlpha(alpha);
    }
    if (auto& rlabel = controls_[static_cast<size_t>(kOscResetBaseSlot + osc)].label)
        rlabel->setAlpha(alpha);
}

void PolySynthUI::setOscWave(int osc, int wave) {
    if (osc < 0 || osc >= kNumOscillators)
        return;
    wave = juce::jlimit(0, 3, wave);
    const int slot = osc * kOscSlotCount;
    controls_[slot].slider->setValue(wave, juce::dontSendNotification);
    if (onParameterChanged)
        onParameterChanged(slot, static_cast<float>(wave));
    updateWaveSelectors();
}

void PolySynthUI::updateWaveSelectors() {
    for (int osc = 0; osc < kNumOscillators; ++osc) {
        const int wave = juce::jlimit(
            0, 3, static_cast<int>(std::round(controls_[osc * kOscSlotCount].slider->getValue())));
        waveSelectors_[static_cast<size_t>(osc)].setSelectedIndex(wave, juce::dontSendNotification);
    }
}

void PolySynthUI::pushFilterCurve() {
    if (!filterCurve_)
        return;
    // Synth filter Type order (0=LP 1=HP 2=BP 3=Notch) -> the curve view's order
    // (0=LP 1=BP 2=HP 3=Notch).
    static constexpr int kTypeToViewMode[4] = {0, 2, 1, 3};
    const int mode = kTypeToViewMode[juce::jlimit(0, 3, filterType_)];
    // Engine 0 = SVF (the synth's only filter family).
    filterCurve_->setRawState(0, mode, filterCutoffHz_, filterRes_, filterDrive_,
                              filterSlope_ == 1);
}

void PolySynthUI::syncFilterCurveFromParam(int paramIndex, float value) {
    switch (paramIndex) {
        case kFilterTypeSlot:
            filterType_ = static_cast<int>(std::round(value));
            updateTypeButtons();
            break;
        case kCutoffSlot:
            filterCutoffHz_ = value;
            break;
        case kResonanceSlot:
            filterRes_ = value;
            break;
        case kFilterDriveSlot:
            filterDrive_ = value;
            break;
        case kFilterSlopeSlot:
            filterSlope_ = static_cast<int>(std::round(value));
            updateSlopeButtons();
            break;
        default:
            return;  // not a filter-curve param
    }
    pushFilterCurve();
}

void PolySynthUI::syncGraphFromParam(int paramIndex, float value) {
    if (paramIndex >= kAmpAttackSlot && paramIndex < kAmpAttackSlot + AdsrGraph::kNumStages)
        ampGraph_->setStageValue(static_cast<AdsrGraph::Stage>(paramIndex - kAmpAttackSlot), value);
    else if (paramIndex >= kFilterAttackSlot &&
             paramIndex < kFilterAttackSlot + AdsrGraph::kNumStages)
        filterGraph_->setStageValue(static_cast<AdsrGraph::Stage>(paramIndex - kFilterAttackSlot),
                                    value);
}

void PolySynthUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    for (const auto& info : params) {
        if (info.paramIndex < 0 || info.paramIndex >= kNumParams)
            continue;
        const int idx = info.paramIndex;
        auto& c = controls_[static_cast<size_t>(idx)];
        c.slider->setParameterInfo(info);
        // Drop the unit suffix on the osc coarse (st) / fine (cents) boxes so it
        // does not clip in the narrow cells; the column label already names them.
        if (idx < kNumOscillators * kOscSlotCount) {
            const int col = idx % kOscSlotCount;
            if (col == 2 || col == 3)
                c.slider->setValueFormatter(
                    [](double v) { return juce::String(juce::roundToInt(v)); });
        }
        c.slider->setValue(info.currentValue, juce::dontSendNotification);

        syncFilterCurveFromParam(idx, info.currentValue);
        if (idx < kNumOscillators * kOscSlotCount && (idx % kOscSlotCount) == 0)
            updateWaveSelectors();  // wave slot -> dropdown

        if (idx == kVoiceModeSlot) {
            voiceMode_ = juce::jlimit(0, kNumVoiceModes - 1,
                                      static_cast<int>(std::round(info.currentValue)));
            updateVoiceModeButtons();
        }
        if (idx >= kOscResetBaseSlot && idx < kOscResetBaseSlot + kNumOscillators) {
            oscReset_[static_cast<size_t>(idx - kOscResetBaseSlot)] = info.currentValue >= 0.5f;
            updateOscResetButtons();
        }
        if (idx >= kOscEnableBaseSlot && idx < kOscEnableBaseSlot + kNumOscillators) {
            const int osc = idx - kOscEnableBaseSlot;
            oscEnabled_[static_cast<size_t>(osc)] = info.currentValue >= 0.5f;
            updateOscEnableButtons();
            applyOscColumnEnabled(osc);
        }

        // Mirror the ADSR slots into their envelope graphs (carries the range too).
        if (idx >= kAmpAttackSlot && idx < kAmpAttackSlot + AdsrGraph::kNumStages)
            ampGraph_->setStage(static_cast<AdsrGraph::Stage>(idx - kAmpAttackSlot), idx, info,
                                info.currentValue);
        else if (idx >= kFilterAttackSlot && idx < kFilterAttackSlot + AdsrGraph::kNumStages)
            filterGraph_->setStage(static_cast<AdsrGraph::Stage>(idx - kFilterAttackSlot), idx,
                                   info, info.currentValue);
    }
}

std::vector<LinkableTextSlider*> PolySynthUI::getLinkableSliders() {
    std::vector<LinkableTextSlider*> sliders;
    sliders.reserve(kNumParams);
    for (auto& c : controls_)
        sliders.push_back(c.slider.get());
    return sliders;
}

void PolySynthUI::layoutOscSection() {
    auto a = oscArea_.reduced(kSectionGap);
    a.removeFromTop(kSectionTitleH);

    // Four oscillator columns side by side. Each column, top to bottom: the enable
    // toggle (header, carries the osc number), the waveform icon selector, the
    // Level / Coarse / Fine value boxes, and a phase-reset toggle pinned to the
    // bottom. The performance controls live in the spare space below.
    constexpr int kHeaderH = 18;
    constexpr int kWaveH = 22;
    constexpr int kResetH = 16;
    constexpr int kColsMaxH = 176;
    const int perfH = (kCellLabelH + 18) + kSectionGap * 2 + (kCellLabelH + 24) + kSectionGap * 2;
    auto colsRegion = a.removeFromTop(juce::jmin(kColsMaxH, juce::jmax(0, a.getHeight() - perfH)));

    const int colW = colsRegion.getWidth() / kNumOscillators;
    for (int osc = 0; osc < kNumOscillators; ++osc) {
        auto col = juce::Rectangle<int>(colsRegion.getX() + osc * colW, colsRegion.getY(), colW,
                                        colsRegion.getHeight())
                       .reduced(kCellPad, 1);
        if (auto& en = oscEnableButtons_[static_cast<size_t>(osc)])
            en->setBounds(col.removeFromTop(kHeaderH));
        col.removeFromTop(2);
        waveSelectors_[static_cast<size_t>(osc)].setBounds(col.removeFromTop(kWaveH));
        col.removeFromTop(2);
        // Reset (retrigger) toggle pinned to the bottom, left-aligned with the
        // column's value boxes; value boxes fill the rest.
        auto resetRow = col.removeFromBottom(kResetH);
        if (auto& rst = oscResetButtons_[static_cast<size_t>(osc)]) {
            const int rw = juce::jmin(24, resetRow.getWidth());
            const int rh = juce::jmin(12, resetRow.getHeight());
            rst->setBounds(resetRow.getX(), resetRow.getCentreY() - rh / 2, rw, rh);
        }
        const int boxH = col.getHeight() / 3;
        for (int p = 1; p < kOscSlotCount; ++p) {  // Level / Coarse / Fine
            auto cell = col.removeFromTop(boxH).reduced(0, 1);
            auto& c = controls_[static_cast<size_t>(osc * kOscSlotCount + p)];
            c.label->setBounds(cell.removeFromTop(kCellLabelH));
            c.slider->setBounds(cell);
        }
    }

    // Performance controls in the spare space below the oscillator columns.
    a.removeFromTop(kSectionGap * 2);
    // Voice Mode segmented buttons, spanning the full OSC-section width.
    auto modeRow = a.removeFromTop(kCellLabelH + 18);
    const int segW = modeRow.getWidth() / kNumVoiceModes;
    for (int v = 0; v < kNumVoiceModes; ++v) {
        if (!voiceModeButtons_[static_cast<size_t>(v)])
            continue;
        auto seg = (v == kNumVoiceModes - 1) ? modeRow
                                             : juce::Rectangle<int>(modeRow.removeFromLeft(segW));
        voiceModeButtons_[static_cast<size_t>(v)]->setBounds(seg);
    }
    a.removeFromTop(kSectionGap * 2);
    // Glide / Bend Range / Vel>Amp / Vel>Cut / Output in one labelled row.
    auto perfRow = a.removeFromTop(kCellLabelH + 24);
    layoutCells(perfRow, {kGlideSlot, kBendRangeSlot, kVelAmpSlot, kVelFilterSlot, kOutputGainSlot},
                5);
}

void PolySynthUI::layoutCells(juce::Rectangle<int> a, const std::vector<int>& indices, int cols) {
    const int n = static_cast<int>(indices.size());
    if (n == 0 || cols <= 0)
        return;
    const int rows = (n + cols - 1) / cols;
    const int cellW = a.getWidth() / cols;
    const int cellH = a.getHeight() / rows;

    for (int i = 0; i < n; ++i) {
        const int col = i % cols;
        const int row = i / cols;
        juce::Rectangle<int> cell(a.getX() + col * cellW, a.getY() + row * cellH, cellW, cellH);
        cell = cell.reduced(kCellPad, 1);

        auto& c = controls_[static_cast<size_t>(indices[static_cast<size_t>(i)])];
        c.label->setBounds(cell.removeFromTop(kCellLabelH));
        c.slider->setBounds(cell);
    }
}

void PolySynthUI::layoutSection(juce::Rectangle<int> area, const std::vector<int>& indices,
                                int cols) {
    auto a = area.reduced(kSectionGap);
    a.removeFromTop(kSectionTitleH);  // painted title strip
    layoutCells(a, indices, cols);
}

void PolySynthUI::layoutAdsrSection(juce::Rectangle<int> area, AdsrGraph* graph,
                                    const std::vector<int>& indices, int cols) {
    auto a = area.reduced(kSectionGap);
    a.removeFromTop(kSectionTitleH);  // painted title strip

    // Envelope graph on top, value boxes (cols-wide grid) beneath.
    const int rows = (static_cast<int>(indices.size()) + cols - 1) / cols;
    const int boxRowH = rows * (kCellLabelH + 20);
    auto boxes = a.removeFromBottom(std::min(boxRowH, a.getHeight() / 2));
    graph->setBounds(a.reduced(2));
    layoutCells(boxes, indices, cols);
}

void PolySynthUI::resized() {
    auto b = getLocalBounds().reduced(2);

    // Three columns: OSC and FILTER (with the response curve) full-height on the
    // left, the two envelopes stacked in a narrower column on the right. The
    // performance controls (Voice Mode + Glide/Bend/Velocity) live in the OSC
    // column's spare space below the oscillator rows.
    const int adsrW = b.getWidth() * 2 / 7;
    const int mainW = b.getWidth() - adsrW;
    // OSC now holds four oscillator columns, so give it a little more than half of
    // the main block; FILTER keeps the response curve + 2x2 grid in the rest.
    const int oscW = mainW * 11 / 20;
    const int adsrX = b.getX() + mainW;
    const int halfH = b.getHeight() / 2;

    oscArea_ = {b.getX(), b.getY(), oscW, b.getHeight()};
    filterArea_ = {b.getX() + oscW, b.getY(), mainW - oscW, b.getHeight()};
    ampArea_ = {adsrX, b.getY(), adsrW, halfH};
    filterEnvArea_ = {adsrX, b.getY() + halfH, adsrW, b.getHeight() - halfH};

    layoutOscSection();
    // Filter section: Type segmented buttons on top, response curve in the
    // middle, the remaining control rows beneath.
    {
        auto a = filterArea_.reduced(kSectionGap);
        auto titleRow = a.removeFromTop(kSectionTitleH);

        // Slope 12/24 toggle on the right of the title row.
        {
            auto slopeArea = titleRow.removeFromRight(96);
            const int segW = slopeArea.getWidth() / kNumSlopes;
            for (int s = 0; s < kNumSlopes; ++s) {
                if (!slopeButtons_[static_cast<size_t>(s)])
                    continue;
                auto seg = (s == kNumSlopes - 1)
                               ? slopeArea
                               : juce::Rectangle<int>(slopeArea.removeFromLeft(segW));
                slopeButtons_[static_cast<size_t>(s)]->setBounds(seg);
            }
        }

        // Type buttons row.
        auto typeRow = a.removeFromTop(kCellLabelH + 18).reduced(kCellPad, 1);
        const int segW = typeRow.getWidth() / kNumFilterTypes;
        for (int t = 0; t < kNumFilterTypes; ++t) {
            if (!typeButtons_[static_cast<size_t>(t)])
                continue;
            auto seg = (t == kNumFilterTypes - 1)
                           ? typeRow
                           : juce::Rectangle<int>(typeRow.removeFromLeft(segW));
            typeButtons_[static_cast<size_t>(t)]->setBounds(seg);
        }
        a.removeFromTop(kSectionGap);

        const std::vector<int> filterCtrls = {kCutoffSlot, kResonanceSlot, kFilterEnvAmtSlot,
                                              kFilterDriveSlot};
        // 2x2 grid for the four value boxes; curve fills the space above them.
        const int rowH = kCellLabelH + 20;
        const int ctrlH = std::min(2 * rowH, a.getHeight() / 2);
        auto ctrlArea = a.removeFromBottom(ctrlH);
        if (filterCurve_)
            filterCurve_->setBounds(a.reduced(2));
        layoutCells(ctrlArea, filterCtrls, 2);
    }
    layoutAdsrSection(ampArea_, ampGraph_.get(),
                      {kAmpAttackSlot, kAmpAttackSlot + 1, kAmpAttackSlot + 2, kAmpAttackSlot + 3});
    layoutAdsrSection(
        filterEnvArea_, filterGraph_.get(),
        {kFilterAttackSlot, kFilterAttackSlot + 1, kFilterAttackSlot + 2, kFilterAttackSlot + 3});
}

void PolySynthUI::paint(juce::Graphics& g) {
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(getLocalBounds());

    const auto border = DarkTheme::getColour(DarkTheme::BORDER);
    const auto titleColour = DarkTheme::getTextColour();
    const auto titleFont = FontManager::getInstance().getUIFont(11.0f);

    auto drawSection = [&](const juce::Rectangle<int>& area, const juce::String& title) {
        auto a = area.reduced(kSectionGap);
        g.setColour(border);
        g.drawRect(a, 1);
        g.setColour(titleColour);
        g.setFont(titleFont);
        g.drawText(title, a.removeFromTop(kSectionTitleH).reduced(kCellPad, 0),
                   juce::Justification::centredLeft);
    };

    drawSection(oscArea_, "OSC");
    drawSection(filterArea_, "FILTER");
    drawSection(ampArea_, "AMP ADSR");
    drawSection(filterEnvArea_, "FILTER ADSR");
}

}  // namespace magda::daw::ui
