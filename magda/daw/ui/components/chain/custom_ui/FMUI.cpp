#include "custom_ui/FMUI.hpp"

#include <cmath>
#include <ranges>

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
constexpr int kHeaderH = 22;
constexpr int kTileGap = 6;
constexpr int kCellGap = 4;
constexpr int kCaptionH = 14;
constexpr int kColumnCaptionH = 14;
constexpr int kRowLabelW = 16;
constexpr int kMinTileH = 30;
constexpr int kMaxTileH = 44;
constexpr int kDot = 6;
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

    for (int op = 0; op < kNumOps; ++op) {
        auto& box = waveBoxes_[static_cast<size_t>(op)];
        box.addItemList({"Sine", "Triangle", "Saw", "Square", "Noise"}, 1);
        box.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
        box.setJustificationType(juce::Justification::centredLeft);
        box.onChange = [this, op] {
            const int wave = waveBoxes_[static_cast<size_t>(op)].getSelectedId() - 1;
            if (wave >= 0)
                setChoiceSlot(kWaveBase + op, wave);
        };
        addAndMakeVisible(box);
    }

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
    for (auto* choice :
         {&positionChoice_, &engineChoice_, &typeChoice_, &combChoice_, &polesChoice_})
        addAndMakeVisible(*choice);

    styleControls();
    refreshOperatorRows();
    refreshFilterPanel();
    refreshEnvelopePanel();
}

FMUI::~FMUI() {
    for (auto& box : waveBoxes_)
        box.setLookAndFeel(nullptr);
}

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
        waveBoxes_[i].setSelectedId(wave + 1, juce::dontSendNotification);

        // A disabled operator's row greys out; its dot stays live to turn it back on.
        const float alpha = opEnabled_[i] ? 1.0f : kInactiveAlpha;
        waveBoxes_[i].setEnabled(opEnabled_[i]);
        waveBoxes_[i].setAlpha(alpha);
        for (int slot : {kRatioBase + op, kLevelBase + op}) {
            controls_[static_cast<size_t>(slot)].slider->setEnabled(opEnabled_[i]);
            controls_[static_cast<size_t>(slot)].slider->setAlpha(alpha);
        }
    }
    repaint();
}

void FMUI::refreshFilterPanel() {
    positionChoice_.setSelectedIndex(filter_.inMatrix ? 1 : 0);
    engineChoice_.setSelectedIndex(filter_.engine);
    typeChoice_.setSelectedIndex(filter_.type);
    combChoice_.setSelectedIndex(filter_.combMode);
    polesChoice_.setSelectedIndex(filter_.poles);
    // Type and poles are the SVF's, the +/- the comb's; the ladder has neither.
    typeChoice_.setVisible(filter_.engine == 0);
    polesChoice_.setVisible(filter_.engine == 0);
    combChoice_.setVisible(filter_.engine == 2);

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

void FMUI::refreshEnvelopePanel() {
    ampGraph_->setVisible(!showFilterEnvelope_);
    filterGraph_->setVisible(showFilterEnvelope_);
    for (int stage = 0; stage < AdsrGraph::kNumStages; ++stage) {
        for (auto [base, shown] : {std::pair{kAmpAdsrBase, !showFilterEnvelope_},
                                   std::pair{kFilterAdsrBase, showFilterEnvelope_}}) {
            auto& c = controls_[static_cast<size_t>(base + stage)];
            c.slider->setVisible(shown && !c.tile.isEmpty());
            c.label->setVisible(shown && !c.tile.isEmpty());
        }
    }
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
        auto inner = bounds.reduced(9, 5);
        c.label->setBounds(inner.removeFromTop(kCaptionH));
        c.label->setVisible(true);
        c.slider->setBounds(inner);
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

namespace {
// The operator and matrix panels share their rows: a column-caption strip, then one
// row per node, so operator n's row lines up with matrix row n.
struct NodeRows {
    juce::Rectangle<int> header, captions;
    std::array<juce::Rectangle<int>, 5> rows;
};

NodeRows nodeRows(juce::Rectangle<int> panel) {
    NodeRows out;
    auto a = panel.reduced(kPanelPadX, kPanelPadY);
    out.header = a.removeFromTop(kHeaderH);
    a.removeFromTop(kTileGap);
    out.captions = a.removeFromTop(kColumnCaptionH);
    a.removeFromTop(kCellGap);
    const int rowH = (a.getHeight() - 4 * kCellGap) / 5;
    for (auto& row : out.rows) {
        row = a.removeFromTop(rowH);
        a.removeFromTop(kCellGap);
    }
    return out;
}
}  // namespace

void FMUI::layoutOperatorPanel() {
    const auto rows = nodeRows(opsPanel_);

    // Columns: Wave | Ratio | Level | Rst, after the row labels.
    static constexpr float kColumns[] = {0.40f, 0.21f, 0.27f};
    const int usableW = rows.captions.getWidth() - kRowLabelW - 3 * kCellGap;
    const auto columns = [&](juce::Rectangle<int> row) {
        std::array<juce::Rectangle<int>, 5> cells;
        cells[0] = row.removeFromLeft(kRowLabelW);
        for (int col = 0; col < 3; ++col) {
            cells[static_cast<size_t>(col + 1)] =
                row.removeFromLeft(juce::roundToInt(usableW * kColumns[col]));
            row.removeFromLeft(kCellGap);
        }
        cells[4] = row;
        return cells;
    };

    const auto captions = columns(rows.captions);
    for (int col = 0; col < 4; ++col)
        opColumnCaptions_[static_cast<size_t>(col)] = captions[static_cast<size_t>(col + 1)];

    for (int node = 0; node < kNodes; ++node) {
        const auto cells = columns(rows.rows[static_cast<size_t>(node)]);
        opRowLabels_[static_cast<size_t>(node)] = cells[0];
        if (node == kNumOps) {
            // The filter node: its level stands in the operators' level column.
            filterRowTile_ = cells[1].getUnion(cells[2]);
            placeTile(kFilterLevelSlot, cells[3], false);
            continue;
        }
        const auto i = static_cast<size_t>(node);
        waveTiles_[i] = cells[1];
        auto waveInner = cells[1].reduced(6, 2);
        enableDots_[i] =
            waveInner.removeFromLeft(kDot + 6).withSizeKeepingCentre(kDot + 6, kDot + 6);
        waveBoxes_[i].setBounds(waveInner.withSizeKeepingCentre(
            waveInner.getWidth(), juce::jmin(waveInner.getHeight(), 22)));
        placeTile(kRatioBase + node, cells[2], false);
        placeTile(kLevelBase + node, cells[3], false);
        resetTiles_[i] = cells[4];
    }
}

void FMUI::layoutMatrixPanel() {
    const auto rows = nodeRows(matrixPanel_);
    auto captions = rows.captions;
    captions.removeFromLeft(kRowLabelW);
    const int cellW = (captions.getWidth() - (kNodes - 1) * kCellGap) / kNodes;
    for (int dst = 0; dst < kNodes; ++dst)
        matrixColLabels_[static_cast<size_t>(dst)] =
            captions.withX(captions.getX() + dst * (cellW + kCellGap)).withWidth(cellW);

    for (int src = 0; src < kNodes; ++src) {
        auto row = rows.rows[static_cast<size_t>(src)];
        matrixRowLabels_[static_cast<size_t>(src)] = row.removeFromLeft(kRowLabelW);
        for (int dst = 0; dst < kNodes; ++dst)
            if (const int slot = matrixSlot(src, dst); slot >= 0)
                placeTile(slot, row.withX(row.getX() + dst * (cellW + kCellGap)).withWidth(cellW),
                          false);
    }
}

void FMUI::layoutFilterPanel() {
    auto a = filterPanel_.reduced(kPanelPadX, kPanelPadY);
    auto header = a.removeFromTop(kHeaderH);
    filterTab_ = takeTab(header, "FILTER", true);
    filterDot_ = filterTab_.withWidth(kDot).withSizeKeepingCentre(kDot + 6, kDot + 6);
    positionChoice_.setBounds(
        header.removeFromRight(juce::jmin(header.getWidth(), positionChoice_.getPreferredWidth())));
    a.removeFromTop(kTileGap);

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

    const int tileH = juce::jlimit(kMinTileH, kMaxTileH, a.getHeight() / 4);
    auto grid = a.removeFromBottom(2 * tileH + kTileGap);
    a.removeFromBottom(kTileGap);
    filterCurve_->setBounds(a);
    placeTiles(grid,
               {kFilterCutoffSlot, kFilterResonanceSlot, kFilterDriveSlot, kFilterKeyTrackSlot,
                kFilterEnvSlot, kFilterVelSlot},
               3);
}

void FMUI::layoutEnvelopePanel() {
    auto a = envelopePanel_.reduced(kPanelPadX, kPanelPadY);
    auto header = a.removeFromTop(kHeaderH);
    envelopeTabs_[0] = takeTab(header, "AMP", false);
    envelopeTabs_[1] = takeTab(header, "FILTER", false);
    a.removeFromTop(kTileGap);

    // Graph, the A/D/S/R tiles, then Glide / Vel / Gain.
    const int tileH = juce::jlimit(kMinTileH, kMaxTileH, a.getHeight() / 6);
    placeTiles(a.removeFromBottom(tileH), {kGlideSlot, kVelAmtSlot, kGainSlot}, 3);
    a.removeFromBottom(kTileGap);
    auto grid = a.removeFromBottom(2 * tileH + kTileGap);
    a.removeFromBottom(kTileGap);
    ampGraph_->setBounds(a);
    filterGraph_->setBounds(a);
    for (int base : {kAmpAdsrBase, kFilterAdsrBase})
        placeTiles(grid, {base, base + 1, base + 2, base + 3}, 2);
}

void FMUI::resized() {
    auto b = getLocalBounds().reduced(kPanelGap);
    const int w = b.getWidth() - 3 * kPanelGap;
    opsPanel_ = b.removeFromLeft(w * 27 / 100);
    b.removeFromLeft(kPanelGap);
    matrixPanel_ = b.removeFromLeft(w * 25 / 100);
    b.removeFromLeft(kPanelGap);
    filterPanel_ = b.removeFromLeft(w * 26 / 100);
    b.removeFromLeft(kPanelGap);
    envelopePanel_ = b;

    layoutOperatorPanel();
    layoutMatrixPanel();
    layoutFilterPanel();
    layoutEnvelopePanel();
    // Placing tiles shows every one; the tab then hides the other envelope's.
    refreshEnvelopePanel();
}

// =============================================================================
// Interaction and paint
// =============================================================================

void FMUI::mouseDown(const juce::MouseEvent& e) {
    const auto p = e.getPosition();
    for (int op = 0; op < kNumOps; ++op) {
        const auto i = static_cast<size_t>(op);
        if (enableDots_[i].contains(p))
            return setChoiceSlot(kEnableBase + op, opEnabled_[i] ? 0 : 1);
        if (resetTiles_[i].contains(p) && opEnabled_[i])
            return setChoiceSlot(kResetBase + op, opReset_[i] ? 0 : 1);
    }
    if (filterDot_.contains(p))
        return setChoiceSlot(kFilterEnableSlot, filter_.on ? 0 : 1);
    for (int tab = 0; tab < 2; ++tab)
        if (envelopeTabs_[static_cast<size_t>(tab)].contains(p) &&
            (tab == 1) != showFilterEnvelope_) {
            showFilterEnvelope_ = tab == 1;
            return refreshEnvelopePanel();
        }
}

void FMUI::lookAndFeelChanged() {
    styleControls();
    repaint();
}

void FMUI::paint(juce::Graphics& g) {
    g.fillAll(colour(ActiveTheme::DEVICE_BG));

    for (const auto& panel : {opsPanel_, matrixPanel_, filterPanel_, envelopePanel_}) {
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

    // Operator rows: wave tile with its enable dot, and the reset checkbox.
    for (int op = 0; op < kNumOps; ++op) {
        const auto i = static_cast<size_t>(op);
        const float alpha = opEnabled_[i] ? 1.0f : kInactiveAlpha;
        field(waveTiles_[i], alpha);
        field(resetTiles_[i], alpha);
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
    g.drawText("Filter node", filterRowTile_.reduced(6, 0), juce::Justification::centredRight,
               true);

    // Column captions and row labels; the filter node in blue.
    g.setFont(titleFont());
    static const juce::String kOpColumns[] = {"WAVE", "RATIO", "LEVEL", "RST"};
    g.setColour(colour(ActiveTheme::DEVICE_DIM));
    for (int col = 0; col < 4; ++col)
        g.drawText(kOpColumns[col], opColumnCaptions_[static_cast<size_t>(col)],
                   col == 3 ? juce::Justification::centred : juce::Justification::centredLeft,
                   false);
    for (int node = 0; node < kNodes; ++node) {
        const auto i = static_cast<size_t>(node);
        g.setColour(colour(node < kNumOps ? ActiveTheme::DEVICE_TITLE : ActiveTheme::DEVICE_BLUE));
        g.drawText(nodeName(node), opRowLabels_[i], juce::Justification::centredLeft, false);
        g.drawText(nodeName(node) + juce::String::fromUTF8("→"), matrixRowLabels_[i],
                   juce::Justification::centredLeft, false);
        g.drawText(juce::String::fromUTF8("→") + nodeName(node), matrixColLabels_[i],
                   juce::Justification::centred, false);
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
    for (auto [panel, title] :
         {std::pair{opsPanel_, "OPERATORS"}, std::pair{matrixPanel_, "MATRIX"}}) {
        auto header = nodeRows(panel).header;
        tab(takeTab(header, title, false), title, true, false);
    }
    tab(filterTab_, "FILTER", true, true);
    dot(filterDot_, filter_.on);
    tab(envelopeTabs_[0], "AMP", !showFilterEnvelope_, false);
    tab(envelopeTabs_[1], "FILTER", showFilterEnvelope_, false);

    const float filterAlpha = filter_.on ? 1.0f : 0.4f;
    g.setColour(colour(ActiveTheme::DEVICE_DIM).withMultipliedAlpha(filterAlpha));
    if (filter_.inMatrix) {
        juce::StringArray sends;
        for (int op = 0; op < kNumOps; ++op)
            if (controls_[static_cast<size_t>(kFilterSendBase + op)].slider->getValue() > 0.0)
                sends.add(juce::String(op + 1));
        g.drawText("IN " + (sends.isEmpty() ? juce::String("-") : sends.joinIntoString(" ")),
                   sendsCaption_, juce::Justification::centredRight, false);
    }
}

}  // namespace magda::daw::ui
