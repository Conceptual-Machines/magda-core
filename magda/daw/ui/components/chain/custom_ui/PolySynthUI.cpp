// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "custom_ui/PolySynthUI.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>

#include "audio/plugins/compiled/MagdaPolySynthCompiledPlugin.hpp"
#include "core/RangesHelpers.hpp"
#include "custom_ui/SamplerTiles.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallComboBoxLookAndFeel.hpp"

namespace magda::daw::ui {

namespace {
constexpr int kPanelGap = 8;
constexpr int kPanelPadX = 10;
constexpr int kPanelPadY = 8;
/// Less under the last tiles than over the header: the footer frames them (#3144).
constexpr int kPanelPadBottom = 4;
constexpr int kHeaderH = 22;
constexpr int kTileGap = 6;
constexpr int kCaptionH = 14;
constexpr int kMinTileH = 26;
constexpr int kMaxTileH = 46;
constexpr int kDot = 6;

juce::Colour colour(magda::ColourRole role) {
    return ActiveTheme::getColour(role);
}

juce::Font titleFont() {
    return FontManager::getInstance().getMonoFont(10.0f).withExtraKerningFactor(0.12f);
}

juce::Font captionFont() {
    return FontManager::getInstance().getUIFont(12.0f);
}

juce::Font compactCaptionFont() {
    return FontManager::getInstance().getUIFont(10.5f);
}

// The oscillator rows' tiles, which take the compact sizes.
std::vector<int> compactSlots() {
    std::vector<int> slots;
    for (int osc = 0; osc < 4; ++osc)
        for (int p = 1; p < 4; ++p)
            slots.push_back(osc * 4 + p);
    return slots;
}

// The SVF's Q (0.5 + 9.5 r) as the curve view's resonance (Q = 0.5 * 80^r).
float curveResonance(float resonance) {
    return std::log(1.0f + 19.0f * resonance) / std::log(80.0f);
}

juce::String oscPrefix(int osc) {
    return "0" + juce::String(osc + 1) + " ";
}

// A tab title with its on/off dot in front, at the start of @p row.
juce::Rectangle<int> takeTab(juce::Rectangle<int>& row, const juce::String& title, bool withDot) {
    const int textW = juce::GlyphArrangement::getStringWidthInt(titleFont(), title);
    auto tab = row.removeFromLeft((withDot ? kDot + 6 : 0) + textW);
    row.removeFromLeft(18);
    return tab;
}
}  // namespace

PolySynthUI::PolySynthUI() {
    std::array<juce::String, kNumParams> captions;
    for (int osc = 0; osc < kNumOscillators; ++osc) {
        const auto base = static_cast<size_t>(osc * kOscSlotCount);
        captions[base + 1] = oscPrefix(osc) + "Lvl";
        captions[base + 2] = oscPrefix(osc) + "Crs";
        captions[base + 3] = oscPrefix(osc) + "Fine";
    }
    for (const auto& f : kFilterSlots) {
        captions[static_cast<size_t>(f.cutoff)] = "Cutoff";
        captions[static_cast<size_t>(f.resonance)] = "Reso";
        captions[static_cast<size_t>(f.envAmt)] = "Env Amt";
        captions[static_cast<size_t>(f.drive)] = "Drive";
    }
    for (int base : {kFilterAttackSlot, kAmpAttackSlot}) {
        captions[static_cast<size_t>(base)] = "Attack";
        captions[static_cast<size_t>(base + 1)] = "Decay";
        captions[static_cast<size_t>(base + 2)] = "Sustain";
        captions[static_cast<size_t>(base + 3)] = "Release";
    }
    captions[kGlideSlot] = "Glide";
    captions[kBendRangeSlot] = "Bend";
    captions[kVelAmpSlot] = "Vel>Amp";
    captions[kVelFilterSlot] = "Vel>Cut";
    captions[kOutputGainSlot] = "Output";
    captions[kFilterBlendSlot] = "Blend";
    captions[kUnisonDetuneSlot] = "Detune";
    captions[kUnisonSpreadSlot] = "Spread";

    for (int i = 0; i < kNumParams; ++i) {
        auto& c = controls_[static_cast<size_t>(i)];

        c.label = std::make_unique<juce::Label>();
        c.label->setText(captions[static_cast<size_t>(i)], juce::dontSendNotification);
        c.label->setBorderSize({});
        c.label->setJustificationType(juce::Justification::centredLeft);
        c.label->setInterceptsMouseClicks(false, false);
        addChildComponent(*c.label);

        c.slider = std::make_unique<LinkableTextSlider>();
        c.slider->setParamIndex(i);
        c.slider->onValueChanged = [this, i](double v) {
            if (onParameterChanged)
                onParameterChanged(i, static_cast<float>(v));
            syncGraphFromParam(i, static_cast<float>(v));
            syncChoiceFromParam(i, static_cast<float>(v));
        };
        // Shown only once placed in a tile; choices, toggles and combos keep theirs hidden.
        addChildComponent(*c.slider);
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
    addChildComponent(*ampGraph_);
    addChildComponent(*filterGraph_);

    filterCurve_ = std::make_unique<CompiledFilterCurveView>("magda_polysynth");
    addAndMakeVisible(*filterCurve_);

    for (int osc = 0; osc < kNumOscillators; ++osc) {
        auto& box = waveBoxes_[static_cast<size_t>(osc)];
        box.addItemList({"Sine", "Saw", "Square", "Triangle"}, 1);
        box.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
        box.setJustificationType(juce::Justification::centredLeft);
        box.onChange = [this, osc] {
            const int wave = waveBoxes_[static_cast<size_t>(osc)].getSelectedId() - 1;
            if (wave >= 0)
                setChoiceSlot(osc * kOscSlotCount, wave);
        };
        addAndMakeVisible(box);
    }

    routingChoice_.setOptions({"Serial", "Parallel"});
    routingChoice_.onChange = [this](int index) { setChoiceSlot(kFilterRoutingSlot, index); };
    typeChoice_.setOptions({"LP", "HP", "BP", "Notch"});  // drawn as their responses
    typeChoice_.onChange = [this](int index) {
        setChoiceSlot(kFilterSlots[static_cast<size_t>(selectedFilter_)].type, index);
    };
    polesChoice_.setOptions({"2", "4", "6"});
    polesChoice_.onChange = [this](int index) {
        setChoiceSlot(kFilterSlots[static_cast<size_t>(selectedFilter_)].poles, index);
    };
    voicesChoice_.setOptions({"1", "2", "4", "8"});
    voicesChoice_.onChange = [this](int index) { setChoiceSlot(kUnisonVoicesSlot, index); };
    voiceModeChoice_.setOptions({"Poly", "Mono", "Legato"});
    voiceModeChoice_.onChange = [this](int index) { setChoiceSlot(kVoiceModeSlot, index); };
    for (auto* choice :
         {&routingChoice_, &typeChoice_, &polesChoice_, &voicesChoice_, &voiceModeChoice_})
        addAndMakeVisible(*choice);

    styleControls();
    refreshOscRows();
    refreshOscPanel();
    refreshFilterPanel();
    refreshEnvelopePanel();
}

PolySynthUI::~PolySynthUI() {
    stopTimer();
    for (auto& box : waveBoxes_)
        box.setLookAndFeel(nullptr);
}

void PolySynthUI::styleControls() {
    for (int i = 0; i < kNumParams; ++i) {
        auto& c = controls_[static_cast<size_t>(i)];
        c.label->setFont(captionFont());
        c.label->setColour(juce::Label::textColourId, colour(ActiveTheme::DEVICE_DIM));
        sampler_tiles::styleValue(*c.slider);
    }
    // The oscillator rows' tiles are narrow: the v1 text cell's sizes fit "-60.0 dB".
    for (int slot : compactSlots()) {
        auto& c = controls_[static_cast<size_t>(slot)];
        c.slider->setFont(FontManager::getInstance().getUIFontMedium(13.5f));
        c.label->setFont(compactCaptionFont());
    }

    for (auto& box : waveBoxes_) {
        box.setColour(juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
        box.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        box.setColour(juce::ComboBox::textColourId, colour(ActiveTheme::DEVICE_VALUE_TEXT));
        box.setColour(juce::ComboBox::arrowColourId, colour(ActiveTheme::DEVICE_DIM));
    }
    filterCurve_->setCurveColour(colour(ActiveTheme::DEVICE_BLUE));
    for (auto* graph : {ampGraph_.get(), filterGraph_.get()})
        graph->setColours(colour(ActiveTheme::DEVICE_GRAPH_BG), colour(ActiveTheme::DEVICE_BLUE));
}

void PolySynthUI::setLivePlugin(magda::daw::audio::compiled::MagdaPolySynthCompiledPlugin* plugin) {
    if (livePlugin_ == plugin)
        return;

    livePlugin_ = plugin;
    liveCurve_.reset();
    liveCurveRefreshPending_ = plugin != nullptr;
    if (livePlugin_ != nullptr) {
        timerCallback();
        startTimerHz(60);
    } else {
        stopTimer();
        pushFilterCurve();
    }
}

void PolySynthUI::setChoiceSlot(int slot, int value) {
    controls_[static_cast<size_t>(slot)].slider->setValue(value, juce::dontSendNotification);
    if (onParameterChanged)
        onParameterChanged(slot, static_cast<float>(value));
    syncChoiceFromParam(slot, static_cast<float>(value));
}

void PolySynthUI::refreshOscRows() {
    for (int osc = 0; osc < kNumOscillators; ++osc) {
        const auto waveSlot = static_cast<size_t>(osc * kOscSlotCount);
        const int wave = juce::jlimit(
            0, 3, static_cast<int>(std::round(controls_[waveSlot].slider->getValue())));
        auto& box = waveBoxes_[static_cast<size_t>(osc)];
        box.setSelectedId(wave + 1, juce::dontSendNotification);

        // A disabled oscillator's row greys out; its dot stays live to turn it back on.
        const bool on = oscEnabled_[static_cast<size_t>(osc)];
        const float alpha = on ? 1.0f : 0.35f;
        box.setEnabled(on);
        box.setAlpha(alpha);
        for (int p = 1; p < kOscSlotCount; ++p) {
            auto& c = controls_[waveSlot + static_cast<size_t>(p)];
            c.slider->setEnabled(on);
            c.slider->setAlpha(alpha);
            c.label->setAlpha(alpha);
        }
    }
    repaint();
}

void PolySynthUI::refreshOscPanel() {
    for (int osc = 0; osc < kNumOscillators; ++osc) {
        waveBoxes_[static_cast<size_t>(osc)].setVisible(!showVoiceTab_);
        for (int p = 1; p < kOscSlotCount; ++p) {
            auto& c = controls_[static_cast<size_t>(osc * kOscSlotCount + p)];
            c.slider->setVisible(!showVoiceTab_ && !c.tile.isEmpty());
            c.label->setVisible(!showVoiceTab_ && !c.tile.isEmpty());
        }
    }
    for (int slot : {kUnisonDetuneSlot, kUnisonSpreadSlot, kGlideSlot, kBendRangeSlot,
                     kOutputGainSlot, kVelAmpSlot, kVelFilterSlot}) {
        auto& c = controls_[static_cast<size_t>(slot)];
        c.slider->setVisible(showVoiceTab_ && !c.tile.isEmpty());
        c.label->setVisible(showVoiceTab_ && !c.tile.isEmpty());
    }
    voicesChoice_.setVisible(showVoiceTab_);
    voiceModeChoice_.setVisible(showVoiceTab_);
    voiceModeChoice_.setSelectedIndex(
        juce::roundToInt(controls_[kVoiceModeSlot].slider->getValue()));

    // Detune and spread act once there is more than one voice.
    const int voices = juce::roundToInt(controls_[kUnisonVoicesSlot].slider->getValue());
    voicesChoice_.setSelectedIndex(voices);
    const float alpha = voices > 0 ? 1.0f : 0.4f;
    for (int slot : {kUnisonDetuneSlot, kUnisonSpreadSlot}) {
        controls_[static_cast<size_t>(slot)].slider->setAlpha(alpha);
        controls_[static_cast<size_t>(slot)].label->setAlpha(alpha);
    }
    repaint();
}

void PolySynthUI::refreshFilterPanel() {
    const auto& state = filterState_[static_cast<size_t>(selectedFilter_)];
    const bool enabled = filterEnabled_[static_cast<size_t>(selectedFilter_)];
    typeChoice_.setSelectedIndex(state.type);
    polesChoice_.setSelectedIndex(state.poles);
    routingChoice_.setSelectedIndex(filterRouting_);

    const float alpha = enabled ? 1.0f : 0.4f;
    for (juce::Component* c :
         {static_cast<juce::Component*>(&typeChoice_), static_cast<juce::Component*>(&polesChoice_),
          static_cast<juce::Component*>(filterCurve_.get())})
        c->setAlpha(alpha);
    for (int f = 0; f < kNumFilters; ++f) {
        const auto& slots = kFilterSlots[static_cast<size_t>(f)];
        for (int slot : {slots.cutoff, slots.resonance, slots.envAmt, slots.drive}) {
            auto& c = controls_[static_cast<size_t>(slot)];
            const bool shown = f == selectedFilter_ && !c.tile.isEmpty();
            c.slider->setVisible(shown);
            c.label->setVisible(shown);
            c.slider->setAlpha(alpha);
            c.label->setAlpha(alpha);
        }
    }
    // Blend mixes the two only in parallel.
    const float blendAlpha = filterRouting_ == 1 ? 1.0f : 0.4f;
    controls_[kFilterBlendSlot].slider->setAlpha(blendAlpha);
    controls_[kFilterBlendSlot].label->setAlpha(blendAlpha);

    liveCurve_.reset();
    if (livePlugin_ != nullptr)
        liveCurveRefreshPending_ = true;
    else
        pushFilterCurve();
    repaint();
}

void PolySynthUI::refreshEnvelopePanel() {
    ampGraph_->setVisible(!showFilterEnvelope_);
    filterGraph_->setVisible(showFilterEnvelope_);
    for (int stage = 0; stage < AdsrGraph::kNumStages; ++stage) {
        for (auto [base, shown] : {std::pair{kAmpAttackSlot, !showFilterEnvelope_},
                                   std::pair{kFilterAttackSlot, showFilterEnvelope_}}) {
            auto& c = controls_[static_cast<size_t>(base + stage)];
            c.slider->setVisible(shown && !c.tile.isEmpty());
            c.label->setVisible(shown && !c.tile.isEmpty());
        }
    }
    repaint();
}

void PolySynthUI::pushFilterCurve() {
    pushFilterCurve(filterState_[static_cast<size_t>(selectedFilter_)]);
}

void PolySynthUI::pushFilterCurve(const CurveState& state) {
    if (!filterCurve_)
        return;
    // Synth filter Type order (0=LP 1=HP 2=BP 3=Notch) -> the curve view's order
    // (0=LP 1=BP 2=HP 3=Notch).
    static constexpr int kTypeToViewMode[4] = {0, 2, 1, 3};
    const int mode = kTypeToViewMode[juce::jlimit(0, 3, state.type)];
    // Engine 0 = SVF (the synth's only filter family).
    filterCurve_->setRawState(0, mode, state.cutoffHz, curveResonance(state.resonance), state.drive,
                              state.poles + 1);
}

void PolySynthUI::timerCallback() {
    if (livePlugin_ == nullptr)
        return;

    const auto readDisplayValue = [this](int slot, float fallback) {
        if (auto parameter = livePlugin_->getSlotParameter(slot)) {
            return livePlugin_->nativeValueToDisplayValue(slot, parameter.currentValue());
        }
        return fallback;
    };

    const auto& slots = kFilterSlots[static_cast<size_t>(selectedFilter_)];
    const auto& base = filterState_[static_cast<size_t>(selectedFilter_)];
    CurveState live;
    live.type =
        static_cast<int>(std::round(readDisplayValue(slots.type, static_cast<float>(base.type))));
    live.cutoffHz = readDisplayValue(slots.cutoff, base.cutoffHz);
    live.resonance = readDisplayValue(slots.resonance, base.resonance);
    live.drive = readDisplayValue(slots.drive, base.drive);
    live.poles =
        static_cast<int>(std::round(readDisplayValue(slots.poles, static_cast<float>(base.poles))));

    if (live == liveCurve_ && !liveCurveRefreshPending_)
        return;
    liveCurve_ = live;
    liveCurveRefreshPending_ = false;
    pushFilterCurve(live);
}

void PolySynthUI::syncChoiceFromParam(int paramIndex, float value) {
    const int choice = static_cast<int>(std::round(value));
    if (paramIndex == kFilterRoutingSlot) {
        filterRouting_ = choice;
        refreshFilterPanel();
        return;
    }
    if (paramIndex == kUnisonVoicesSlot || paramIndex == kVoiceModeSlot) {
        refreshOscPanel();
        return;
    }
    if (paramIndex < kNumOscillators * kOscSlotCount && paramIndex % kOscSlotCount == 0) {
        refreshOscRows();
        return;
    }
    if (paramIndex >= kOscResetBaseSlot && paramIndex < kOscResetBaseSlot + kNumOscillators) {
        oscReset_[static_cast<size_t>(paramIndex - kOscResetBaseSlot)] = choice != 0;
        repaint();
        return;
    }
    if (paramIndex >= kOscEnableBaseSlot && paramIndex < kOscEnableBaseSlot + kNumOscillators) {
        oscEnabled_[static_cast<size_t>(paramIndex - kOscEnableBaseSlot)] = choice != 0;
        refreshOscRows();
        return;
    }
    for (int f = 0; f < kNumFilters; ++f) {
        const auto& slots = kFilterSlots[static_cast<size_t>(f)];
        auto& state = filterState_[static_cast<size_t>(f)];
        if (paramIndex == slots.type)
            state.type = choice;
        else if (paramIndex == slots.cutoff)
            state.cutoffHz = value;
        else if (paramIndex == slots.resonance)
            state.resonance = value;
        else if (paramIndex == slots.drive)
            state.drive = value;
        else if (paramIndex == slots.poles)
            state.poles = choice;
        else if (paramIndex == slots.enable)
            filterEnabled_[static_cast<size_t>(f)] = choice != 0;
        else
            continue;
        if (f == selectedFilter_ || paramIndex == slots.enable)
            refreshFilterPanel();
        return;
    }
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
        // does not clip in the narrow cells; the caption already names them.
        if (idx < kNumOscillators * kOscSlotCount) {
            const int col = idx % kOscSlotCount;
            if (col == 2 || col == 3)
                c.slider->setValueFormatter(
                    [](double v) { return juce::String(juce::roundToInt(v)); });
        }
        c.slider->setValue(info.currentValue, juce::dontSendNotification);

        syncGraphFromParam(idx, info.currentValue);
        syncChoiceFromParam(idx, info.currentValue);

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
    const auto sliderOf = [](const auto& control) { return control.slider.get(); };

    return controls_ | std::views::transform(sliderOf) | toStd<std::vector<LinkableTextSlider*>>();
}

// =============================================================================
// Layout
// =============================================================================

void PolySynthUI::placeTile(int slot, juce::Rectangle<int> bounds) {
    auto& c = controls_[static_cast<size_t>(slot)];
    c.tile = bounds;
    const bool compact = std::ranges::contains(compactSlots(), slot);
    sampler_tiles::layoutCaptioned(*c.label, *c.slider, bounds.reduced(compact ? 7 : 9, 4),
                                   compact ? compactCaptionFont() : captionFont(),
                                   compact ? 13.5f : 15.0f);
    c.label->setVisible(true);
    c.slider->setVisible(true);
}

void PolySynthUI::placeTiles(juce::Rectangle<int> area, const std::vector<int>& slots, int cols) {
    const int n = static_cast<int>(slots.size());
    const int rows = (n + cols - 1) / cols;
    const int tileW = (area.getWidth() - (cols - 1) * kTileGap) / cols;
    const int tileH = (area.getHeight() - (rows - 1) * kTileGap) / rows;
    for (int i = 0; i < n; ++i)
        placeTile(slots[static_cast<size_t>(i)],
                  {area.getX() + (i % cols) * (tileW + kTileGap),
                   area.getY() + (i / cols) * (tileH + kTileGap), tileW, tileH});
}

void PolySynthUI::layoutOscPanel() {
    auto a =
        oscPanel_.reduced(kPanelPadX, kPanelPadY).withTrimmedBottom(kPanelPadBottom - kPanelPadY);
    auto header = a.removeFromTop(kHeaderH);
    oscTabs_[0] = takeTab(header, "OSC", false);
    oscTabs_[1] = takeTab(header, "VOICE", false);
    a.removeFromTop(kTileGap);

    // VOICE: the unison stack, the performance controls three across, Voice Mode at the foot.
    const int tileH = juce::jlimit(kMinTileH, kMaxTileH, (a.getHeight() - 3 * kTileGap) / 4);
    auto voice = a;
    auto unison = voice.removeFromTop(tileH);
    voice.removeFromTop(kTileGap);
    const int tileW = (unison.getWidth() - 2 * kTileGap) / 3;
    voicesTile_ = unison.withWidth(tileW);
    auto voicesInner = voicesTile_.reduced(9, 5);
    voicesInner.removeFromTop(kCaptionH);
    voicesChoice_.setBounds(voicesInner.withSizeKeepingCentre(
        juce::jmin(voicesInner.getWidth(), voicesChoice_.getPreferredWidth()),
        juce::jmin(voicesInner.getHeight(), 20)));
    placeTile(kUnisonDetuneSlot, unison.withX(unison.getX() + tileW + kTileGap).withWidth(tileW));
    placeTile(kUnisonSpreadSlot,
              unison.withX(unison.getX() + 2 * (tileW + kTileGap)).withWidth(tileW));
    placeTiles(voice.removeFromTop(2 * tileH + kTileGap),
               {kGlideSlot, kBendRangeSlot, kOutputGainSlot, kVelAmpSlot, kVelFilterSlot}, 3);
    voiceModeTile_ = voice.removeFromBottom(tileH);
    auto voiceModeInner = voiceModeTile_.reduced(9, 5);
    voiceModeInner.removeFromTop(kCaptionH);
    voiceModeChoice_.setBounds(voiceModeInner.withSizeKeepingCentre(
        juce::jmin(voiceModeInner.getWidth(), voiceModeChoice_.getPreferredWidth()),
        juce::jmin(voiceModeInner.getHeight(), 20)));

    // OSC: a row per oscillator, Wave | Level | Coarse | Fine | Reset, as the mock's columns.
    static constexpr float kColumns[] = {0.28f, 0.22f, 0.16f, 0.17f};
    const int rowH = juce::jmin(kMaxTileH + 8, (a.getHeight() - 3 * kTileGap) / kNumOscillators);
    const int usableW = a.getWidth() - 4 * kTileGap;
    for (int osc = 0; osc < kNumOscillators; ++osc) {
        auto row = a.removeFromTop(rowH);
        a.removeFromTop(kTileGap);
        const auto take = [&](float share) {
            auto tile = row.removeFromLeft(juce::roundToInt(usableW * share));
            row.removeFromLeft(kTileGap);
            return tile;
        };
        const auto i = static_cast<size_t>(osc);
        waveTiles_[i] = take(kColumns[0]);
        auto waveInner = waveTiles_[i].reduced(7, 5);
        auto caption = waveInner.removeFromTop(kCaptionH);
        enableDots_[i] = caption.removeFromRight(kDot).withSizeKeepingCentre(kDot + 6, kDot + 6);
        waveBoxes_[i].setBounds(waveInner.withTrimmedLeft(-4));
        for (int p = 1; p < kOscSlotCount; ++p)
            placeTile(osc * kOscSlotCount + p, take(kColumns[p]));
        resetTiles_[i] = row;
    }
}

void PolySynthUI::layoutFilterPanel() {
    auto a = filterPanel_.reduced(kPanelPadX, kPanelPadY)
                 .withTrimmedBottom(kPanelPadBottom - kPanelPadY);

    // Tabs on the left, the shared routing and blend on the right.
    auto header = a.removeFromTop(kHeaderH);
    for (int f = 0; f < kNumFilters; ++f) {
        filterTabs_[static_cast<size_t>(f)] =
            takeTab(header, "FILTER " + juce::String(f + 1), true);
        filterDots_[static_cast<size_t>(f)] =
            filterTabs_[static_cast<size_t>(f)].withWidth(kDot).withSizeKeepingCentre(kDot + 6,
                                                                                      kDot + 6);
    }
    routingChoice_.setBounds(header.removeFromRight(routingChoice_.getPreferredWidth()));
    a.removeFromTop(kTileGap);

    auto choiceRow = a.removeFromTop(kHeaderH);
    typeChoice_.setBounds(choiceRow.removeFromLeft(typeChoice_.getPreferredWidth()));
    polesChoice_.setBounds(choiceRow.removeFromRight(polesChoice_.getPreferredWidth()));
    polesCaption_ = choiceRow.removeFromRight(46);
    a.removeFromTop(kTileGap);

    // Both filters' tiles share the 2x2 grid; only the selected tab's show.
    const int tileH = juce::jlimit(kMinTileH, kMaxTileH, a.getHeight() / 5);
    auto grid = a.removeFromBottom(2 * tileH + kTileGap);
    a.removeFromBottom(kTileGap);
    filterCurve_->setBounds(a);
    // Blend mixes the two filters, so it stands beside either tab's grid.
    placeTile(kFilterBlendSlot, grid.removeFromRight(grid.getWidth() / 4));
    grid.removeFromRight(kTileGap);
    for (const auto& f : kFilterSlots)
        placeTiles(grid, {f.cutoff, f.resonance, f.envAmt, f.drive}, 2);
}

void PolySynthUI::layoutEnvelopePanel() {
    auto a = envelopePanel_.reduced(kPanelPadX, kPanelPadY)
                 .withTrimmedBottom(kPanelPadBottom - kPanelPadY);
    auto header = a.removeFromTop(kHeaderH);
    envelopeTabs_[0] = takeTab(header, "AMP ADSR", false);
    envelopeTabs_[1] = takeTab(header, "FILTER ADSR", false);
    a.removeFromTop(kTileGap);

    const int tileH = juce::jlimit(kMinTileH, kMaxTileH, a.getHeight() / 5);
    auto grid = a.removeFromBottom(2 * tileH + kTileGap);
    a.removeFromBottom(kTileGap);
    ampGraph_->setBounds(a);
    filterGraph_->setBounds(a);
    for (int base : {kAmpAttackSlot, kFilterAttackSlot})
        placeTiles(grid, {base, base + 1, base + 2, base + 3}, 2);
}

void PolySynthUI::resized() {
    // Padded at the sides only: the header and the voice-mode footer already frame the panels.
    auto b = getLocalBounds().reduced(kPanelGap, 0);
    const int oscW = b.getWidth() * 38 / 100;
    const int filterW = b.getWidth() * 36 / 100;

    oscPanel_ = b.removeFromLeft(oscW);
    b.removeFromLeft(kPanelGap);
    filterPanel_ = b.removeFromLeft(filterW);
    b.removeFromLeft(kPanelGap);
    envelopePanel_ = b;

    layoutOscPanel();
    layoutFilterPanel();
    layoutEnvelopePanel();
    // Placing tiles shows every one; the tabs then hide the unselected ones.
    refreshOscPanel();
    refreshFilterPanel();
    refreshEnvelopePanel();
}

// =============================================================================
// Interaction and paint
// =============================================================================

void PolySynthUI::mouseDown(const juce::MouseEvent& e) {
    const auto p = e.getPosition();
    for (int tab = 0; tab < 2; ++tab)
        if (oscTabs_[static_cast<size_t>(tab)].contains(p) && (tab == 1) != showVoiceTab_) {
            showVoiceTab_ = tab == 1;
            return refreshOscPanel();
        }
    for (int osc = 0; osc < kNumOscillators && !showVoiceTab_; ++osc) {
        const auto i = static_cast<size_t>(osc);
        if (enableDots_[i].contains(p))
            return setChoiceSlot(kOscEnableBaseSlot + osc, oscEnabled_[i] ? 0 : 1);
        if (resetTiles_[i].contains(p) && oscEnabled_[i])
            return setChoiceSlot(kOscResetBaseSlot + osc, oscReset_[i] ? 0 : 1);
    }
    for (int f = 0; f < kNumFilters; ++f) {
        const auto i = static_cast<size_t>(f);
        if (filterDots_[i].contains(p))
            return setChoiceSlot(kFilterSlots[i].enable, filterEnabled_[i] ? 0 : 1);
        if (filterTabs_[i].contains(p) && f != selectedFilter_) {
            selectedFilter_ = f;
            return refreshFilterPanel();
        }
    }
    for (int tab = 0; tab < 2; ++tab)
        if (envelopeTabs_[static_cast<size_t>(tab)].contains(p) &&
            (tab == 1) != showFilterEnvelope_) {
            showFilterEnvelope_ = tab == 1;
            return refreshEnvelopePanel();
        }
}

void PolySynthUI::lookAndFeelChanged() {
    styleControls();
    repaint();
}

void PolySynthUI::paint(juce::Graphics& g) {
    g.fillAll(colour(ActiveTheme::DEVICE_BG));

    for (const auto& panel : {oscPanel_, filterPanel_, envelopePanel_}) {
        g.setColour(colour(ActiveTheme::DEVICE_PANEL));
        g.fillRoundedRectangle(panel.toFloat(), 6.0f);
        g.setColour(colour(ActiveTheme::DEVICE_LINE));
        g.drawRoundedRectangle(panel.toFloat().reduced(0.5f), 6.0f, 1.0f);
    }

    const auto field = [&g](juce::Rectangle<int> tile, float alpha) {
        if (tile.isEmpty())
            return;
        g.beginTransparencyLayer(alpha);
        sampler_tiles::paint(g, tile, 4.0f);
        g.endTransparencyLayer();
    };
    const auto dot = [&g](juce::Rectangle<int> hit, bool on) {
        g.setColour(colour(on ? ActiveTheme::DEVICE_BLUE : ActiveTheme::DEVICE_SWITCH_OFF));
        g.fillEllipse(hit.toFloat().withSizeKeepingCentre(kDot, kDot));
    };

    for (const auto& c : controls_)
        if (c.slider->isVisible())
            field(c.tile, c.slider->getAlpha());

    if (showVoiceTab_) {
        field(voicesTile_, 1.0f);
        g.setFont(captionFont());
        g.setColour(colour(ActiveTheme::DEVICE_DIM));
        g.drawText("Voices", voicesTile_.reduced(9, 5).removeFromTop(kCaptionH),
                   juce::Justification::centredLeft, true);
        field(voiceModeTile_, 1.0f);
        g.setColour(colour(ActiveTheme::DEVICE_DIM));
        g.drawText("Voice Mode", voiceModeTile_.reduced(9, 5).removeFromTop(kCaptionH),
                   juce::Justification::centredLeft, true);
    }

    // Oscillator rows: the wave tile with its enable dot, and the reset checkbox.
    g.setFont(compactCaptionFont());
    for (int osc = 0; osc < kNumOscillators && !showVoiceTab_; ++osc) {
        const auto i = static_cast<size_t>(osc);
        const float alpha = oscEnabled_[i] ? 1.0f : 0.35f;
        field(waveTiles_[i], alpha);
        field(resetTiles_[i], alpha);
        g.setColour(colour(ActiveTheme::DEVICE_DIM).withMultipliedAlpha(alpha));
        g.drawText(oscPrefix(osc) + "Wave", waveTiles_[i].reduced(7, 5).removeFromTop(kCaptionH),
                   juce::Justification::centredLeft, true);
        auto resetInner = resetTiles_[i].reduced(7, 5);
        g.drawText("Rst", resetInner.removeFromTop(kCaptionH), juce::Justification::centredLeft,
                   true);
        const auto box = resetInner.withWidth(12).withSizeKeepingCentre(12, 12).toFloat();
        if (oscReset_[i]) {
            g.setColour(colour(ActiveTheme::DEVICE_BLUE).withMultipliedAlpha(alpha));
            g.fillRoundedRectangle(box, 2.0f);
        }
        g.setColour(
            colour(ActiveTheme::DEVICE_FIELD_BORDER).brighter(0.3f).withMultipliedAlpha(alpha));
        g.drawRoundedRectangle(box.reduced(0.5f), 2.0f, 1.0f);
        dot(enableDots_[i], oscEnabled_[i]);
    }

    // Titles and tabs: the selected tab lit and underlined.
    g.setFont(titleFont());
    const auto tab = [&](juce::Rectangle<int> area, const juce::String& title, bool selected,
                         bool withDot) {
        g.setColour(colour(selected ? ActiveTheme::DEVICE_TITLE : ActiveTheme::DEVICE_DIM));
        g.drawText(title, withDot ? area.withTrimmedLeft(kDot + 6) : area,
                   juce::Justification::centredLeft, false);
        if (selected) {
            g.setColour(colour(ActiveTheme::DEVICE_BLUE));
            g.fillRect(area.withTop(area.getBottom() - 2));
        }
    };
    tab(oscTabs_[0], "OSC", !showVoiceTab_, false);
    tab(oscTabs_[1], "VOICE", showVoiceTab_, false);
    for (int f = 0; f < kNumFilters; ++f) {
        const auto i = static_cast<size_t>(f);
        tab(filterTabs_[i], "FILTER " + juce::String(f + 1), f == selectedFilter_, true);
        dot(filterDots_[i], filterEnabled_[i]);
    }
    tab(envelopeTabs_[0], "AMP ADSR", !showFilterEnvelope_, false);
    tab(envelopeTabs_[1], "FILTER ADSR", showFilterEnvelope_, false);
    g.setColour(colour(ActiveTheme::DEVICE_DIM)
                    .withMultipliedAlpha(
                        filterEnabled_[static_cast<size_t>(selectedFilter_)] ? 1.0f : 0.4f));
    g.drawText("POLES", polesCaption_, juce::Justification::centred, false);
}

}  // namespace magda::daw::ui
