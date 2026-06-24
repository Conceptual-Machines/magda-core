#include "custom_ui/PolySynthUI.hpp"

#include <algorithm>
#include <cmath>

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

namespace {
constexpr int kSectionTitleH = 15;
constexpr int kCellLabelH = 12;
constexpr int kCellPad = 3;
constexpr int kSectionGap = 4;
}  // namespace

PolySynthUI::PolySynthUI() {
    // Short per-slot labels. The oscillator cells are prefixed with the
    // oscillator number so each cell is self-describing in the dense grid.
    for (int osc = 0; osc < kNumOscillators; ++osc) {
        const int base = osc * kOscSlotCount;
        const juce::String p = "O" + juce::String(osc + 1) + " ";
        labels_[static_cast<size_t>(base + 0)] = p + "Wave";
        labels_[static_cast<size_t>(base + 1)] = p + "Lvl";
        labels_[static_cast<size_t>(base + 2)] = p + "Crs";
        labels_[static_cast<size_t>(base + 3)] = p + "Fine";
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

    // The Type/Slope value boxes are replaced by the buttons; keep the objects
    // (for linking/value) but hide them.
    controls_[kFilterTypeSlot].slider->setVisible(false);
    controls_[kFilterTypeSlot].label->setVisible(false);
    controls_[kFilterSlopeSlot].slider->setVisible(false);
    controls_[kFilterSlopeSlot].label->setVisible(false);
    updateTypeButtons();
    updateSlopeButtons();
}

PolySynthUI::~PolySynthUI() {
    for (auto& btn : typeButtons_)
        if (btn)
            btn->setLookAndFeel(nullptr);
    for (auto& btn : slopeButtons_)
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
        c.slider->setValue(info.currentValue, juce::dontSendNotification);

        syncFilterCurveFromParam(idx, info.currentValue);

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
                                    const std::vector<int>& indices) {
    auto a = area.reduced(kSectionGap);
    a.removeFromTop(kSectionTitleH);  // painted title strip

    // Envelope graph on top, value boxes (one row) beneath.
    const int boxRowH = kCellLabelH + 22;
    auto boxes = a.removeFromBottom(std::min(boxRowH, a.getHeight() / 2));
    graph->setBounds(a.reduced(2));
    layoutCells(boxes, indices, static_cast<int>(indices.size()));
}

void PolySynthUI::resized() {
    auto b = getLocalBounds().reduced(2);
    const int halfW = b.getWidth() / 2;
    const int halfH = b.getHeight() / 2;

    oscArea_ = {b.getX(), b.getY(), halfW, halfH};
    filterArea_ = {b.getX() + halfW, b.getY(), b.getWidth() - halfW, halfH};
    ampArea_ = {b.getX(), b.getY() + halfH, halfW, b.getHeight() - halfH};
    filterEnvArea_ = {b.getX() + halfW, b.getY() + halfH, b.getWidth() - halfW,
                      b.getHeight() - halfH};

    std::vector<int> oscParams;
    oscParams.reserve(kNumOscillators * kOscSlotCount);
    for (int i = 0; i < kNumOscillators * kOscSlotCount; ++i)
        oscParams.push_back(i);

    layoutSection(oscArea_, oscParams, kOscSlotCount);  // 4 osc rows x 4 controls
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
