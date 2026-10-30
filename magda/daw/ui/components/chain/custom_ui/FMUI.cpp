// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "custom_ui/FMUI.hpp"

#include <BinaryData.h>

#include <cmath>
#include <ranges>

#include "core/RangesHelpers.hpp"
#include "custom_ui/SamplerTiles.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {
constexpr int kPanelGap = 8;
constexpr int kPanelPadX = 10;
constexpr int kPanelPadY = 8;
/// Less under the last tiles than over the header: the footer frames them (#3144).
constexpr int kPanelPadBottom = 4;
constexpr int kHeaderH = 22;
constexpr int kTileGap = 6;
constexpr int kCellGap = 4;
constexpr int kCaptionH = 14;
constexpr int kMinTileH = 26;
constexpr int kMaxTileH = 44;
constexpr int kDot = 6;
// The operator table's columns, each as wide as its values: Wave (dot and icon), Ratio, Level
// ("-12.0 dB") and Rst (#3201).
constexpr std::array<int, 4> kOpColumnW{44, 46, 62, 26};
constexpr int kSendW = 40;
constexpr int kRowLabelW = 14;
constexpr int kColumnNamesH = 14;
constexpr int kOpsPanelW =
    2 * kPanelPadX + kOpColumnW[0] + kOpColumnW[1] + kOpColumnW[2] + kOpColumnW[3] + 3 * kCellGap;
// A row label then a send cell per node.
constexpr int kMatrixPanelW = 2 * kPanelPadX + kRowLabelW + 5 * (kCellGap + kSendW);
constexpr int kFilterPanelW = 404;
constexpr float kInactiveAlpha = 0.35f;

juce::Colour colour(magda::ColourRole role) {
    return ActiveTheme::getColour(role);
}

juce::Font titleFont() {
    return FontManager::getInstance().getMonoFont(10.0f).withExtraKerningFactor(0.12f);
}

juce::Font captionFont() {
    return FontManager::getInstance().getUIFont(12.0f);
}

juce::Rectangle<int> takeTab(juce::Rectangle<int>& row, const juce::String& title, bool withDot) {
    const int textW = juce::GlyphArrangement::getStringWidthInt(titleFont(), title);
    auto tab = row.removeFromLeft((withDot ? kDot + 6 : 0) + textW);
    row.removeFromLeft(18);
    return tab;
}

juce::String nodeName(int node) {
    return node < 4 ? juce::String(node + 1) : juce::String("F");
}

// Engine order (SVF / Ladder / Comb) -> CompiledFilterCurveView's engine index.
constexpr int kEngineToView[3] = {0, 1, 6};
// Type order (LP / HP / BP / Notch) -> the curve view's (LP / BP / HP / Notch).
constexpr int kTypeToViewMode[4] = {0, 2, 1, 3};
}  // namespace

int FMUI::matrixSlot(int src, int dst) {
    if (src < kNumOps && dst < kNumOps)
        return kMatrixBase + src * kNumOps + dst;
    if (src < kNumOps)
        return kFilterSendBase + src;
    if (dst < kNumOps)
        return kFilterReturnBase + dst;
    return -1;
}

FMUI::FMUI() {
    std::array<juce::String, kNumParams> captions;
    for (int base : {kAmpAdsrBase, kFilterAdsrBase}) {
        captions[static_cast<size_t>(base)] = "Attack";
        captions[static_cast<size_t>(base + 1)] = "Decay";
        captions[static_cast<size_t>(base + 2)] = "Sustain";
        captions[static_cast<size_t>(base + 3)] = "Release";
    }
    captions[kGlideSlot] = "Glide";
    captions[kBendRangeSlot] = "Bend";
    captions[kVelAmtSlot] = "Vel>Amp";
    captions[kGainSlot] = "Gain";
    captions[kFilterCutoffSlot] = "Cutoff";
    captions[kFilterResonanceSlot] = "Reso";
    captions[kFilterDriveSlot] = "Drive";
    captions[kFilterKeyTrackSlot] = "Key";
    captions[kFilterEnvSlot] = "Env Amt";
    captions[kFilterVelSlot] = "Vel>Cut";

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
            syncFromParam(i, static_cast<float>(v));
        };
        // Shown only once placed in a tile; choices, toggles and combos keep theirs hidden.
        addChildComponent(*c.slider);
    }

    // Dragging an envelope handle writes the plugin value AND keeps the matching
    // value box in sync, so the boxes stay authoritative for linking/automation.
    ampGraph_ = std::make_unique<AdsrGraph>();
    filterGraph_ = std::make_unique<AdsrGraph>();
    for (auto* graph : {ampGraph_.get(), filterGraph_.get()}) {
        graph->onStageChanged = [this](int paramIndex, float value) {
            if (onParameterChanged)
                onParameterChanged(paramIndex, value);
            if (paramIndex >= 0 && paramIndex < kNumParams)
                controls_[static_cast<size_t>(paramIndex)].slider->setValue(
                    value, juce::dontSendNotification);
        };
        addChildComponent(*graph);
    }

    filterCurve_ = std::make_unique<CompiledFilterCurveView>("magda_fm");
    addAndMakeVisible(*filterCurve_);

    const std::array<std::pair<const char*, int>, 5> icons{
        {{BinaryData::fadmodsine_svg, BinaryData::fadmodsine_svgSize},
         {BinaryData::fadmodtri_svg, BinaryData::fadmodtri_svgSize},
         {BinaryData::fadmodsawup_svg, BinaryData::fadmodsawup_svgSize},
         {BinaryData::fadmodsquare_svg, BinaryData::fadmodsquare_svgSize},
         {BinaryData::fadmodrandom_svg, BinaryData::fadmodrandom_svgSize}}};
    for (size_t wave = 0; wave < icons.size(); ++wave)
        waveIcons_[wave] = juce::Drawable::createFromImageData(
            icons[wave].first, static_cast<size_t>(icons[wave].second));

    positionChoice_.setOptions({"Output", "Matrix"});
    positionChoice_.onChange = [this](int index) { setChoiceSlot(kFilterPositionSlot, index); };
    engineChoice_.setOptions({"SVF", "Ladder", "Comb"});
    engineChoice_.onChange = [this](int index) { setChoiceSlot(kFilterEngineSlot, index); };
    typeChoice_.setOptions({"LP", "HP", "BP", "Notch"});  // drawn as their responses
    typeChoice_.onChange = [this](int index) { setChoiceSlot(kFilterTypeSlot, index); };
    combChoice_.setOptions({"+", "-"});
    combChoice_.onChange = [this](int index) { setChoiceSlot(kFilterCombModeSlot, index); };
    polesChoice_.setOptions({"2", "4", "6"});
    polesChoice_.onChange = [this](int index) { setChoiceSlot(kFilterPolesSlot, index); };
    voiceModeChoice_.setOptions({"Poly", "Mono", "Legato"});
    voiceModeChoice_.onChange = [this](int index) { setChoiceSlot(kVoiceModeSlot, index); };
    for (auto* choice : {&positionChoice_, &engineChoice_, &typeChoice_, &combChoice_,
                         &polesChoice_, &voiceModeChoice_})
        addAndMakeVisible(*choice);

    styleControls();
    refreshOperatorRows();
    refreshFirstPanel();
    refreshMatrix();
    refreshFilterPanel();
}

FMUI::~FMUI() = default;

void FMUI::styleControls() {
    for (auto& c : controls_) {
        c.label->setFont(captionFont());
        c.label->setColour(juce::Label::textColourId, colour(ActiveTheme::DEVICE_DIM));
        sampler_tiles::styleValue(*c.slider);
    }
    // The operator and matrix cells are narrow: the v1 text cell's compact size.
    for (int src = 0; src < kNodes; ++src)
        for (int dst = 0; dst < kNodes; ++dst)
            if (const int slot = matrixSlot(src, dst); slot >= 0)
                controls_[static_cast<size_t>(slot)].slider->setFont(
                    FontManager::getInstance().getUIFontMedium(13.5f));
    for (int op = 0; op < kNumOps; ++op)
        for (int slot : {kRatioBase + op, kLevelBase + op})
            controls_[static_cast<size_t>(slot)].slider->setFont(
                FontManager::getInstance().getUIFontMedium(13.5f));
    controls_[kFilterLevelSlot].slider->setFont(FontManager::getInstance().getUIFontMedium(13.5f));

    filterCurve_->setCurveColour(colour(ActiveTheme::DEVICE_BLUE));
    for (auto* graph : {ampGraph_.get(), filterGraph_.get()})
        graph->setColours(colour(ActiveTheme::DEVICE_GRAPH_BG), colour(ActiveTheme::DEVICE_BLUE));
}

// =============================================================================
// Values
// =============================================================================

void FMUI::setChoiceSlot(int slot, int value) {
    controls_[static_cast<size_t>(slot)].slider->setValue(value, juce::dontSendNotification);
    if (onParameterChanged)
        onParameterChanged(slot, static_cast<float>(value));
    syncFromParam(slot, static_cast<float>(value));
}

void FMUI::syncFromParam(int slot, float value) {
    const int choice = static_cast<int>(std::round(value));
    if (slot >= kWaveBase && slot < kWaveBase + kNumOps)
        return refreshOperatorRows();
    if (slot >= kResetBase && slot < kResetBase + kNumOps) {
        opReset_[static_cast<size_t>(slot - kResetBase)] = choice != 0;
        return repaint();
    }
    if (slot >= kEnableBase && slot < kEnableBase + kNumOps) {
        opEnabled_[static_cast<size_t>(slot - kEnableBase)] = choice != 0;
        return refreshOperatorRows();
    }
    if (slot >= kFilterSendBase && slot < kFilterSendBase + kNumOps)
        return repaint();  // the FILTER panel lists the sends
    if (slot == kVoiceModeSlot)
        return voiceModeChoice_.setSelectedIndex(juce::jlimit(0, 2, choice));

    switch (slot) {
        case kFilterEnableSlot:
            filter_.on = choice != 0;
            break;
        case kFilterPositionSlot:
            filter_.inMatrix = choice != 0;
            break;
        case kFilterEngineSlot:
            filter_.engine = juce::jlimit(0, 2, choice);
            break;
        case kFilterTypeSlot:
            filter_.type = juce::jlimit(0, 3, choice);
            break;
        case kFilterCombModeSlot:
            filter_.combMode = juce::jlimit(0, 1, choice);
            break;
        case kFilterPolesSlot:
            filter_.poles = juce::jlimit(0, 2, choice);
            break;
        case kFilterCutoffSlot:
            filter_.cutoffHz = value;
            break;
        case kFilterResonanceSlot:
            filter_.resonance = value;
            break;
        case kFilterDriveSlot:
            filter_.drive = value;
            break;
        default:
            return;
    }
    refreshFilterPanel();
}

void FMUI::syncGraphFromParam(int slot, float value) {
    if (slot >= kAmpAdsrBase && slot < kAmpAdsrBase + AdsrGraph::kNumStages)
        ampGraph_->setStageValue(static_cast<AdsrGraph::Stage>(slot - kAmpAdsrBase), value);
    else if (slot >= kFilterAdsrBase && slot < kFilterAdsrBase + AdsrGraph::kNumStages)
        filterGraph_->setStageValue(static_cast<AdsrGraph::Stage>(slot - kFilterAdsrBase), value);
}

void FMUI::refreshOperatorRows() {
    for (int op = 0; op < kNumOps; ++op) {
        const auto i = static_cast<size_t>(op);
        const int wave = juce::jlimit(
            0, 4, static_cast<int>(std::round(controls_[kWaveBase + i].slider->getValue())));
        opWave_[i] = wave;

        // A disabled operator's row greys out; its dot stays live to turn it back on.
        const float alpha = opEnabled_[i] ? 1.0f : kInactiveAlpha;
        for (int slot : {kRatioBase + op, kLevelBase + op}) {
            controls_[static_cast<size_t>(slot)].slider->setEnabled(opEnabled_[i]);
            controls_[static_cast<size_t>(slot)].slider->setAlpha(alpha);
        }
    }
    repaint();
}

void FMUI::showSlot(int slot, bool shown, bool withLabel) {
    auto& c = controls_[static_cast<size_t>(slot)];
    c.slider->setVisible(shown && !c.tile.isEmpty());
    c.label->setVisible(withLabel && shown && !c.tile.isEmpty());
}

void FMUI::refreshFirstPanel() {
    const bool ops = shows(FirstTab::Ops);
    const bool voice = shows(FirstTab::Voice);
    for (int op = 0; op < kNumOps; ++op)
        for (int slot : {kRatioBase + op, kLevelBase + op})
            showSlot(slot, ops, false);
    showSlot(kFilterLevelSlot, ops, false);
    ampGraph_->setVisible(shows(FirstTab::AmpEnv));
    for (int stage = 0; stage < AdsrGraph::kNumStages; ++stage)
        showSlot(kAmpAdsrBase + stage, shows(FirstTab::AmpEnv), true);
    for (int slot : {kGlideSlot, kBendRangeSlot, kVelAmtSlot, kGainSlot})
        showSlot(slot, voice, true);
    voiceModeChoice_.setVisible(voice);
    repaint();
}

void FMUI::refreshMatrix() {
    for (int src = 0; src < kNodes; ++src)
        for (int dst = 0; dst < kNodes; ++dst)
            if (const int slot = matrixSlot(src, dst); slot >= 0)
                showSlot(slot, matrixShown_, false);
    repaint();
}

void FMUI::refreshFilterPanel() {
    positionChoice_.setSelectedIndex(filter_.inMatrix ? 1 : 0);
    engineChoice_.setSelectedIndex(filter_.engine);
    typeChoice_.setSelectedIndex(filter_.type);
    combChoice_.setSelectedIndex(filter_.combMode);
    polesChoice_.setSelectedIndex(filter_.poles);

    // Type and poles are the SVF's, the +/- the comb's; the ladder has neither.
    const bool filterTab = !showFilterEnvelope_;
    positionChoice_.setVisible(filterTab);
    engineChoice_.setVisible(filterTab);
    filterCurve_->setVisible(filterTab);
    typeChoice_.setVisible(filterTab && filter_.engine == 0);
    polesChoice_.setVisible(filterTab && filter_.engine == 0);
    combChoice_.setVisible(filterTab && filter_.engine == 2);
    for (int slot : {kFilterCutoffSlot, kFilterResonanceSlot, kFilterDriveSlot, kFilterKeyTrackSlot,
                     kFilterEnvSlot, kFilterVelSlot})
        showSlot(slot, filterTab, true);
    filterGraph_->setVisible(!filterTab);
    for (int stage = 0; stage < AdsrGraph::kNumStages; ++stage)
        showSlot(kFilterAdsrBase + stage, !filterTab, true);

    const float alpha = filter_.on ? 1.0f : 0.4f;
    for (juce::Component* c :
         {static_cast<juce::Component*>(&positionChoice_),
          static_cast<juce::Component*>(&engineChoice_),
          static_cast<juce::Component*>(&typeChoice_), static_cast<juce::Component*>(&combChoice_),
          static_cast<juce::Component*>(&polesChoice_),
          static_cast<juce::Component*>(filterCurve_.get())})
        c->setAlpha(alpha);
    for (int slot = kFilterCutoffSlot; slot <= kFilterEnvSlot; ++slot) {
        controls_[static_cast<size_t>(slot)].slider->setAlpha(alpha);
        controls_[static_cast<size_t>(slot)].label->setAlpha(alpha);
    }

    const int mode = filter_.engine == 2 ? filter_.combMode : kTypeToViewMode[filter_.type];
    filterCurve_->setRawState(kEngineToView[filter_.engine], mode, filter_.cutoffHz,
                              filter_.resonance, filter_.drive, filter_.poles + 1);
    repaint();
}

void FMUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    for (const auto& info : params) {
        if (info.paramIndex < 0 || info.paramIndex >= kNumParams)
            continue;
        const int idx = info.paramIndex;
        auto& c = controls_[static_cast<size_t>(idx)];
        c.slider->setParameterInfo(info);
        // Matrix cells are dense: the raw amount with no unit.
        if (idx < kNumOps * kNumOps ||
            (idx >= kFilterSendBase && idx < kFilterReturnBase + kNumOps))
            c.slider->setValueFormatter([](double v) { return juce::String(v, 2); });
        c.slider->setValue(info.currentValue, juce::dontSendNotification);

        if (idx >= kAmpAdsrBase && idx < kAmpAdsrBase + AdsrGraph::kNumStages)
            ampGraph_->setStage(static_cast<AdsrGraph::Stage>(idx - kAmpAdsrBase), idx, info,
                                info.currentValue);
        else if (idx >= kFilterAdsrBase && idx < kFilterAdsrBase + AdsrGraph::kNumStages)
            filterGraph_->setStage(static_cast<AdsrGraph::Stage>(idx - kFilterAdsrBase), idx, info,
                                   info.currentValue);
        syncFromParam(idx, info.currentValue);
    }
}

std::vector<LinkableTextSlider*> FMUI::getLinkableSliders() {
    const auto sliderOf = [](const auto& control) { return control.slider.get(); };

    return controls_ | std::views::transform(sliderOf) | toStd<std::vector<LinkableTextSlider*>>();
}

// =============================================================================
// Layout
// =============================================================================

void FMUI::placeTile(int slot, juce::Rectangle<int> bounds, bool withCaption) {
    auto& c = controls_[static_cast<size_t>(slot)];
    c.tile = bounds;
    if (withCaption) {
        sampler_tiles::layoutCaptioned(*c.label, *c.slider, bounds.reduced(9, 4), captionFont(),
                                       15.0f);
        c.label->setVisible(true);
    } else {
        c.slider->setBounds(bounds.reduced(4, 2));
    }
    c.slider->setVisible(true);
}

void FMUI::placeTiles(juce::Rectangle<int> area, const std::vector<int>& slots, int cols) {
    const int n = static_cast<int>(slots.size());
    const int rows = (n + cols - 1) / cols;
    const int tileW = (area.getWidth() - (cols - 1) * kTileGap) / cols;
    const int tileH = (area.getHeight() - (rows - 1) * kTileGap) / rows;
    for (int i = 0; i < n; ++i)
        placeTile(slots[static_cast<size_t>(i)],
                  {area.getX() + (i % cols) * (tileW + kTileGap),
                   area.getY() + (i / cols) * (tileH + kTileGap), tileW, tileH});
}

// A row per operator, its Wave | Ratio | Level | Rst, then the filter's level; the columns'
// names head it.
void FMUI::layoutOperators(juce::Rectangle<int> a) {
    const auto names = a.removeFromTop(kColumnNamesH);
    a.removeFromTop(kCellGap);
    const int rowH = (a.getHeight() - (kNodes - 1) * kCellGap) / kNodes;

    const auto columns = [](juce::Rectangle<int> row) {
        std::array<juce::Rectangle<int>, 4> cells;
        for (size_t col = 0; col < cells.size(); ++col) {
            cells[col] = row.removeFromLeft(kOpColumnW[col]);
            row.removeFromLeft(kCellGap);
        }
        return cells;
    };

    opColumnCaptions_ = columns(names);
    for (int node = 0; node < kNodes; ++node) {
        const auto cells = columns(a.withY(a.getY() + node * (rowH + kCellGap)).withHeight(rowH));
        if (node == kNumOps) {
            // The filter node: its level stands in the operators' level column.
            filterRowTile_ = cells[0].getUnion(cells[1]);
            placeTile(kFilterLevelSlot, cells[2], false);
            continue;
        }
        const auto i = static_cast<size_t>(node);
        waveTiles_[i] = cells[0];
        auto waveInner = cells[0].reduced(4, 2);
        enableDots_[i] =
            waveInner.removeFromLeft(kDot + 6).withSizeKeepingCentre(kDot + 6, kDot + 6);
        waveIconAreas_[i] = waveInner;
        placeTile(kRatioBase + node, cells[1], false);
        placeTile(kLevelBase + node, cells[2], false);
        resetTiles_[i] = cells[3];
    }
}

// The sends, a row per source node under a column per destination.
void FMUI::layoutMatrix() {
    auto area = matrixPanel_.reduced(kPanelPadX, kPanelPadY)
                    .withTrimmedBottom(kPanelPadBottom - kPanelPadY);
    const auto columns = [](juce::Rectangle<int> row) {
        std::array<juce::Rectangle<int>, 1 + kNodes> cells;
        cells[0] = row.removeFromLeft(kRowLabelW);
        for (int dst = 0; dst < kNodes; ++dst) {
            row.removeFromLeft(kCellGap);
            cells[static_cast<size_t>(1 + dst)] = row.removeFromLeft(kSendW);
        }
        return cells;
    };

    const auto heads = columns(area.removeFromTop(kColumnNamesH));
    for (int dst = 0; dst < kNodes; ++dst)
        matrixColLabels_[static_cast<size_t>(dst)] = heads[static_cast<size_t>(1 + dst)];
    area.removeFromTop(kCellGap);
    const int rowH = (area.getHeight() - (kNodes - 1) * kCellGap) / kNodes;
    for (int src = 0; src < kNodes; ++src) {
        const auto cells =
            columns(area.withY(area.getY() + src * (rowH + kCellGap)).withHeight(rowH));
        matrixRowLabels_[static_cast<size_t>(src)] = cells[0];
        for (int dst = 0; dst < kNodes; ++dst)
            if (const int slot = matrixSlot(src, dst); slot >= 0)
                placeTile(slot, cells[static_cast<size_t>(1 + dst)], false);
    }
}

// The graph, then the A/D/S/R tiles.
void FMUI::layoutEnvelope(juce::Rectangle<int> area, AdsrGraph& graph, int adsrBase) {
    const int tileH = juce::jlimit(kMinTileH, kMaxTileH, area.getHeight() / 6);
    auto grid = area.removeFromBottom(2 * tileH + kTileGap);
    area.removeFromBottom(kTileGap);
    graph.setBounds(area);
    placeTiles(grid, {adsrBase, adsrBase + 1, adsrBase + 2, adsrBase + 3}, 2);
}

// OPS, AMP ENV and VOICE share the first panel as tabs.
void FMUI::layoutFirstPanel() {
    auto a =
        firstPanel_.reduced(kPanelPadX, kPanelPadY).withTrimmedBottom(kPanelPadBottom - kPanelPadY);
    auto header = a.removeFromTop(kHeaderH);
    firstTabs_[0] = takeTab(header, "OPS", false);
    firstTabs_[1] = takeTab(header, "AMP ENV", false);
    firstTabs_[2] = takeTab(header, "VOICE", false);
    a.removeFromTop(kTileGap);
    layoutOperators(a);
    layoutEnvelope(a, *ampGraph_, kAmpAdsrBase);
    layoutVoice(a);
}

void FMUI::showWaveMenu(int op) {
    static constexpr const char* kNames[] = {"Sine", "Triangle", "Saw", "Square", "Noise"};
    juce::PopupMenu menu;
    for (int wave = 0; wave < 5; ++wave) {
        auto icon = waveIcons_[static_cast<size_t>(wave)]->createCopy();
        icon->replaceColour(juce::Colours::black, colour(ActiveTheme::DEVICE_TEXT));
        menu.addItem(wave + 1, kNames[wave], true, wave == opWave_[static_cast<size_t>(op)],
                     std::move(icon));
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                           localAreaToGlobal(waveIconAreas_[static_cast<size_t>(op)])),
                       [safe = juce::Component::SafePointer(this), op](int result) {
                           if (safe != nullptr && result > 0)
                               safe->setChoiceSlot(kWaveBase + op, result - 1);
                       });
}

void FMUI::layoutFilterPanel() {
    auto a = filterPanel_.reduced(kPanelPadX, kPanelPadY)
                 .withTrimmedBottom(kPanelPadBottom - kPanelPadY);
    auto header = a.removeFromTop(kHeaderH);
    filterTabs_[0] = takeTab(header, "FILTER", true);
    filterTabs_[1] = takeTab(header, "FILTER ENV", false);
    filterDot_ = filterTabs_[0].withWidth(kDot).withSizeKeepingCentre(kDot + 6, kDot + 6);
    positionChoice_.setBounds(
        header.removeFromRight(juce::jmin(header.getWidth(), positionChoice_.getPreferredWidth())));
    a.removeFromTop(kTileGap);
    layoutEnvelope(a, *filterGraph_, kFilterAdsrBase);

    // Engine, then the SVF's poles on the right.
    auto engineRow = a.removeFromTop(kHeaderH);
    engineChoice_.setBounds(engineRow.removeFromLeft(
        juce::jmin(engineRow.getWidth(), engineChoice_.getPreferredWidth())));
    polesChoice_.setBounds(engineRow.removeFromRight(
        juce::jmin(engineRow.getWidth(), polesChoice_.getPreferredWidth())));
    a.removeFromTop(kTileGap);

    // Type (SVF) or +/- (comb), and the node's incoming sends on the right.
    auto typeRow = a.removeFromTop(kHeaderH);
    typeChoice_.setBounds(typeRow.withWidth(typeChoice_.getPreferredWidth()));
    combChoice_.setBounds(typeRow.withWidth(combChoice_.getPreferredWidth()));
    sendsCaption_ = typeRow.removeFromRight(
        juce::jmax(0, typeRow.getWidth() - typeChoice_.getPreferredWidth() - kTileGap));
    a.removeFromTop(kTileGap);

    // The curve beside the tiles, as tall as they are (#3201).
    filterCurve_->setBounds(a.removeFromRight(a.getWidth() * 2 / 5));
    a.removeFromRight(kTileGap);
    placeTiles(a,
               {kFilterCutoffSlot, kFilterResonanceSlot, kFilterDriveSlot, kFilterKeyTrackSlot,
                kFilterEnvSlot, kFilterVelSlot},
               2);
}

// Voice Mode across, then Glide and Bend, then Gain and Velocity.
void FMUI::layoutVoice(juce::Rectangle<int> a) {
    const int tileH = juce::jlimit(kMinTileH, kMaxTileH, (a.getHeight() - 2 * kTileGap) / 3);
    voiceModeTile_ = a.removeFromTop(tileH);
    auto voiceModeInner = voiceModeTile_.reduced(9, 4);
    voiceModeInner.removeFromTop(kCaptionH);
    voiceModeChoice_.setBounds(voiceModeInner.withSizeKeepingCentre(
        juce::jmin(voiceModeInner.getWidth(), voiceModeChoice_.getPreferredWidth()),
        juce::jmin(voiceModeInner.getHeight(), 20)));
    a.removeFromTop(kTileGap);
    placeTiles(a.removeFromTop(2 * tileH + kTileGap),
               {kGlideSlot, kBendRangeSlot, kGainSlot, kVelAmtSlot}, 2);
}

void FMUI::setPanelsShown(bool opsShown, bool matrixShown) {
    if (opsShown == opsShown_ && matrixShown == matrixShown_)
        return;
    opsShown_ = opsShown;
    matrixShown_ = matrixShown;
    resized();
}

int FMUI::preferredWidth() const {
    return 2 * kPanelGap + kFilterPanelW + (opsShown_ ? kOpsPanelW + kPanelGap : 0) +
           (matrixShown_ ? kMatrixPanelW + kPanelGap : 0);
}

void FMUI::resized() {
    // Padded at the sides only: the header and the voice-mode footer already frame the panels.
    auto b = getLocalBounds().reduced(kPanelGap, 0);
    firstPanel_ = {};
    firstTabs_ = {};
    if (opsShown_) {
        firstPanel_ = b.removeFromLeft(kOpsPanelW);
        b.removeFromLeft(kPanelGap);
    }
    matrixPanel_ = {};
    if (matrixShown_) {
        matrixPanel_ = b.removeFromLeft(kMatrixPanelW);
        b.removeFromLeft(kPanelGap);
    }
    filterPanel_ = b;

    if (opsShown_)
        layoutFirstPanel();
    if (matrixShown_)
        layoutMatrix();
    layoutFilterPanel();
    // Placing tiles shows every one; the tabs then hide the others.
    refreshFirstPanel();
    refreshMatrix();
    refreshFilterPanel();
}

// =============================================================================
// Interaction and paint
// =============================================================================

void FMUI::mouseDown(const juce::MouseEvent& e) {
    const auto p = e.getPosition();
    for (int op = 0; op < kNumOps && shows(FirstTab::Ops); ++op) {
        const auto i = static_cast<size_t>(op);
        if (enableDots_[i].contains(p))
            return setChoiceSlot(kEnableBase + op, opEnabled_[i] ? 0 : 1);
        if (waveIconAreas_[i].contains(p) && opEnabled_[i])
            return showWaveMenu(op);
        if (resetTiles_[i].contains(p) && opEnabled_[i])
            return setChoiceSlot(kResetBase + op, opReset_[i] ? 0 : 1);
    }
    if (filterDot_.contains(p))
        return setChoiceSlot(kFilterEnableSlot, filter_.on ? 0 : 1);
    for (int tab = 0; tab < 3; ++tab)
        if (firstTabs_[static_cast<size_t>(tab)].contains(p)) {
            firstTab_ = static_cast<FirstTab>(tab);
            return refreshFirstPanel();
        }
    for (int tab = 0; tab < 2; ++tab) {
        const bool second = tab == 1;
        if (filterTabs_[static_cast<size_t>(tab)].contains(p) && second != showFilterEnvelope_) {
            showFilterEnvelope_ = second;
            return refreshFilterPanel();
        }
    }
}

void FMUI::lookAndFeelChanged() {
    styleControls();
    repaint();
}

void FMUI::paint(juce::Graphics& g) {
    g.fillAll(colour(ActiveTheme::DEVICE_BG));

    for (const auto& panel : {firstPanel_, matrixPanel_, filterPanel_}) {
        if (panel.isEmpty())
            continue;
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

    const bool ops = shows(FirstTab::Ops);
    if (shows(FirstTab::Voice)) {
        field(voiceModeTile_, 1.0f);
        g.setFont(captionFont());
        g.setColour(colour(ActiveTheme::DEVICE_DIM));
        g.drawText("Voice Mode", voiceModeTile_.reduced(9, 4).removeFromTop(kCaptionH),
                   juce::Justification::centredLeft, true);
    }

    // Operator rows: wave tile with its enable dot, and the reset checkbox.
    for (int op = 0; op < kNumOps && ops; ++op) {
        const auto i = static_cast<size_t>(op);
        const float alpha = opEnabled_[i] ? 1.0f : kInactiveAlpha;
        field(waveTiles_[i], alpha);
        field(resetTiles_[i], alpha);
        if (const auto& icon = waveIcons_[static_cast<size_t>(opWave_[i])]) {
            auto tinted = icon->createCopy();
            tinted->replaceColour(juce::Colours::black, colour(ActiveTheme::DEVICE_VALUE_TEXT));
            tinted->drawWithin(g, waveIconAreas_[i].toFloat().withSizeKeepingCentre(16.0f, 16.0f),
                               juce::RectanglePlacement::centred, alpha);
        }
        const auto box = resetTiles_[i].toFloat().withSizeKeepingCentre(12.0f, 12.0f);
        if (opReset_[i]) {
            g.setColour(colour(ActiveTheme::DEVICE_BLUE).withMultipliedAlpha(alpha));
            g.fillRoundedRectangle(box, 2.0f);
        }
        g.setColour(
            colour(ActiveTheme::DEVICE_FIELD_BORDER).brighter(0.3f).withMultipliedAlpha(alpha));
        g.drawRoundedRectangle(box.reduced(0.5f), 2.0f, 1.0f);
        dot(enableDots_[i], opEnabled_[i]);
    }
    g.setFont(captionFont());
    g.setColour(colour(ActiveTheme::DEVICE_DIM));
    if (ops)
        g.drawText("Filter", filterRowTile_.reduced(6, 0), juce::Justification::centredLeft, true);

    // The table's column names, and the matrix's node names with the filter in blue.
    g.setFont(titleFont());
    if (ops) {
        static const juce::String kOpColumns[] = {"WAVE", "RATIO", "LEVEL", "RST"};
        g.setColour(colour(ActiveTheme::DEVICE_DIM));
        for (size_t col = 0; col < opColumnCaptions_.size(); ++col)
            g.drawText(kOpColumns[col], opColumnCaptions_[col],
                       col == 3 ? juce::Justification::centred : juce::Justification::centredLeft,
                       false);
    }
    if (matrixShown_) {
        for (int node = 0; node < kNodes; ++node) {
            const auto i = static_cast<size_t>(node);
            g.setColour(
                colour(node < kNumOps ? ActiveTheme::DEVICE_TITLE : ActiveTheme::DEVICE_BLUE));
            g.drawText(juce::String::fromUTF8("→") + nodeName(node), matrixColLabels_[i],
                       juce::Justification::centred, false);
            g.drawText(nodeName(node), matrixRowLabels_[i], juce::Justification::centredLeft,
                       false);
        }
    }

    // Titles and tabs: the selected tab lit and underlined.
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
    tab(firstTabs_[0], "OPS", ops, false);
    tab(firstTabs_[1], "AMP ENV", shows(FirstTab::AmpEnv), false);
    tab(firstTabs_[2], "VOICE", shows(FirstTab::Voice), false);
    tab(filterTabs_[0], "FILTER", !showFilterEnvelope_, true);
    tab(filterTabs_[1], "FILTER ENV", showFilterEnvelope_, false);
    dot(filterDot_, filter_.on);

    const float filterAlpha = filter_.on ? 1.0f : 0.4f;
    g.setColour(colour(ActiveTheme::DEVICE_DIM).withMultipliedAlpha(filterAlpha));
    if (filter_.inMatrix && !showFilterEnvelope_) {
        juce::StringArray sends;
        for (int op = 0; op < kNumOps; ++op)
            if (controls_[static_cast<size_t>(kFilterSendBase + op)].slider->getValue() > 0.0)
                sends.add(juce::String(op + 1));
        g.drawText("IN " + (sends.isEmpty() ? juce::String("-") : sends.joinIntoString(" ")),
                   sendsCaption_, juce::Justification::centredRight, false);
    }
}

}  // namespace magda::daw::ui
