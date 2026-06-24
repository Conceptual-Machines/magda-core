#include "custom_ui/PolySynthUI.hpp"

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

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
    layoutSection(filterArea_, {kFilterTypeSlot, kCutoffSlot, kResonanceSlot, kFilterEnvAmtSlot},
                  1);
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
