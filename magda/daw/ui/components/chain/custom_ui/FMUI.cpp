#include "custom_ui/FMUI.hpp"

#include <algorithm>
#include <cmath>

#include "BinaryData.h"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {
constexpr int kSectionTitleH = 15;
constexpr int kCellLabelH = 12;
constexpr int kCellPad = 3;
constexpr int kMatrixHeaderH = 14;
constexpr int kMatrixHeaderW = 22;

// Waveform icons, in the dsp's wave order: 0 Sine / 1 Triangle / 2 Saw / 3
// Square / 4 Noise.
void populateWaveSelector(IconSelector& sel) {
    sel.addOption(BinaryData::fadmodsine_svg, BinaryData::fadmodsine_svgSize, "Sine");
    sel.addOption(BinaryData::fadmodtri_svg, BinaryData::fadmodtri_svgSize, "Triangle");
    sel.addOption(BinaryData::fadmodsawup_svg, BinaryData::fadmodsawup_svgSize, "Saw");
    sel.addOption(BinaryData::fadmodsquare_svg, BinaryData::fadmodsquare_svgSize, "Square");
    sel.addOption(BinaryData::fadmodrandom_svg, BinaryData::fadmodrandom_svgSize, "Noise");
}
}  // namespace

FMUI::FMUI() {
    auto makeLabel = [this](int i, const juce::String& text) {
        auto& c = controls_[static_cast<size_t>(i)];
        c.label = std::make_unique<juce::Label>();
        c.label->setText(text, juce::dontSendNotification);
        c.label->setFont(FontManager::getInstance().getUIFont(10.0f));
        c.label->setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
        c.label->setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(*c.label);
    };

    for (int i = 0; i < kNumParams; ++i) {
        auto& c = controls_[static_cast<size_t>(i)];
        c.slider = std::make_unique<LinkableTextSlider>();
        c.slider->setParamIndex(i);
        const int idx = i;
        c.slider->onValueChanged = [this, idx](double v) {
            if (onParameterChanged)
                onParameterChanged(idx, static_cast<float>(v));
        };
        addAndMakeVisible(*c.slider);
    }

    // Labels: matrix cells are headed by the grid (no per-cell label); the op and
    // amp controls get short labels.
    for (int op = 0; op < kNumOps; ++op) {
        makeLabel(kRatioBase + op, "Ratio");
        makeLabel(kLevelBase + op, "Level");
        makeLabel(kWaveBase + op, "Wave");
    }
    makeLabel(kAmpAdsrBase + 0, "A");
    makeLabel(kAmpAdsrBase + 1, "D");
    makeLabel(kAmpAdsrBase + 2, "S");
    makeLabel(kAmpAdsrBase + 3, "R");
    makeLabel(kGlideSlot, "Glide");
    makeLabel(kVelAmtSlot, "Vel");
    makeLabel(kVoiceModeSlot, "Mode");
    makeLabel(kGainSlot, "Gain");
    for (int op = 0; op < kNumOps; ++op)
        makeLabel(kResetBase + op, "Rst");

    // Per-operator waveform icon selectors overlay the hidden wave sliders.
    for (int op = 0; op < kNumOps; ++op) {
        auto& sel = waveSelectors_[static_cast<size_t>(op)];
        populateWaveSelector(sel);
        sel.onChange = [this, op](int wave) { setOpWave(op, wave); };
        addAndMakeVisible(sel);
        controls_[static_cast<size_t>(kWaveBase + op)].slider->setVisible(false);
    }
}

FMUI::~FMUI() = default;

void FMUI::setOpWave(int op, int wave) {
    if (op < 0 || op >= kNumOps)
        return;
    wave = juce::jlimit(0, kNumWaves - 1, wave);
    const int slot = kWaveBase + op;
    controls_[static_cast<size_t>(slot)].slider->setValue(wave, juce::dontSendNotification);
    if (onParameterChanged)
        onParameterChanged(slot, static_cast<float>(wave));
    updateWaveSelectors();
}

void FMUI::updateWaveSelectors() {
    for (int op = 0; op < kNumOps; ++op) {
        const int wave =
            juce::jlimit(0, kNumWaves - 1,
                         static_cast<int>(std::round(
                             controls_[static_cast<size_t>(kWaveBase + op)].slider->getValue())));
        waveSelectors_[static_cast<size_t>(op)].setSelectedIndex(wave, juce::dontSendNotification);
    }
}

void FMUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    for (const auto& info : params) {
        if (info.paramIndex < 0 || info.paramIndex >= kNumParams)
            continue;
        const int idx = info.paramIndex;
        auto& c = controls_[static_cast<size_t>(idx)];
        c.slider->setParameterInfo(info);
        // Matrix cells are dense: show the raw amount with no unit clutter.
        if (idx >= kMatrixBase && idx < kMatrixBase + kNumOps * kNumOps)
            c.slider->setValueFormatter([](double v) { return juce::String(v, 2); });
        c.slider->setValue(info.currentValue, juce::dontSendNotification);
        if (idx >= kWaveBase && idx < kWaveBase + kNumOps)
            updateWaveSelectors();
    }
}

std::vector<LinkableTextSlider*> FMUI::getLinkableSliders() {
    std::vector<LinkableTextSlider*> sliders;
    sliders.reserve(kNumParams);
    for (auto& c : controls_)
        sliders.push_back(c.slider.get());
    return sliders;
}

void FMUI::layoutCells(juce::Rectangle<int> area, const std::vector<int>& indices, int cols) {
    if (indices.empty() || cols <= 0)
        return;
    const int rows = (static_cast<int>(indices.size()) + cols - 1) / cols;
    const int cw = area.getWidth() / cols;
    const int ch = area.getHeight() / rows;
    for (size_t n = 0; n < indices.size(); ++n) {
        const int r = static_cast<int>(n) / cols;
        const int col = static_cast<int>(n) % cols;
        juce::Rectangle<int> cell(area.getX() + col * cw, area.getY() + r * ch, cw, ch);
        cell = cell.reduced(kCellPad);
        auto& c = controls_[static_cast<size_t>(indices[n])];
        if (c.label) {
            c.label->setBounds(cell.removeFromTop(kCellLabelH));
        }
        c.slider->setBounds(cell);
    }
}

void FMUI::resized() {
    auto r = getLocalBounds().reduced(6);

    // Amp ADSR + Gain live in a column on the right (like PolySynth), so the
    // matrix and operators get the full left side instead of being squeezed
    // above a bottom strip.
    const int ampW = juce::jlimit(86, 150, r.getWidth() / 4);
    ampArea_ = r.removeFromRight(ampW);
    r.removeFromRight(6);  // gap between the left block and the amp column

    // Left block: matrix on top, operators beneath.
    matrixArea_ = r.removeFromTop(r.getHeight() * 60 / 100);
    opsArea_ = r;

    // --- Matrix 4x4 ---
    {
        auto a = matrixArea_;
        a.removeFromTop(kSectionTitleH);
        a.removeFromTop(kMatrixHeaderH);   // column-header row (painted)
        a.removeFromLeft(kMatrixHeaderW);  // row-header column (painted)
        const int cw = a.getWidth() / kNumOps;
        const int chh = a.getHeight() / kNumOps;
        for (int src = 0; src < kNumOps; ++src)
            for (int dst = 0; dst < kNumOps; ++dst) {
                juce::Rectangle<int> cell(a.getX() + dst * cw, a.getY() + src * chh, cw, chh);
                controls_[static_cast<size_t>(kMatrixBase + src * kNumOps + dst)].slider->setBounds(
                    cell.reduced(kCellPad));
            }
    }

    // --- Operator columns: Wave / Ratio / Level ---
    // Reserve the same left strip the matrix uses for its row headers so the
    // operator columns line up directly under the matrix columns.
    {
        auto a = opsArea_;
        a.removeFromTop(kSectionTitleH);
        a.removeFromLeft(kMatrixHeaderW);
        const int colW = a.getWidth() / kNumOps;
        for (int op = 0; op < kNumOps; ++op) {
            auto col = juce::Rectangle<int>(a.getX() + op * colW, a.getY(), colW, a.getHeight())
                           .reduced(kCellPad);
            const int rowH = col.getHeight() / 4;  // Wave / Ratio / Level / Reset
            // Wave icon selector (hidden slider tracks beneath it).
            auto waveRow = col.removeFromTop(rowH);
            controls_[static_cast<size_t>(kWaveBase + op)].slider->setBounds(waveRow);
            waveSelectors_[static_cast<size_t>(op)].setBounds(waveRow.reduced(0, kCellLabelH / 2));
            auto place = [&](int idx, juce::Rectangle<int> cell) {
                auto& c = controls_[static_cast<size_t>(idx)];
                if (c.label)
                    c.label->setBounds(cell.removeFromTop(kCellLabelH));
                c.slider->setBounds(cell);
            };
            place(kRatioBase + op, col.removeFromTop(rowH));
            place(kLevelBase + op, col.removeFromTop(rowH));
            place(kResetBase + op, col.removeFromTop(rowH));
        }
    }

    // --- Right column, stacked: amp ADSR, Glide, Vel, Gain, Voice Mode ---
    {
        auto a = ampArea_;
        a.removeFromTop(kSectionTitleH);
        layoutCells(a,
                    {kAmpAdsrBase + 0, kAmpAdsrBase + 1, kAmpAdsrBase + 2, kAmpAdsrBase + 3,
                     kGlideSlot, kVelAmtSlot, kGainSlot, kVoiceModeSlot},
                    1);
    }
}

void FMUI::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

    auto title = [&](juce::Rectangle<int> area, const juce::String& text) {
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.setFont(FontManager::getInstance().getUIFontBold(9.0f));
        g.drawText(text, area.removeFromTop(kSectionTitleH).reduced(2, 0),
                   juce::Justification::centredLeft);
    };
    title(matrixArea_, "MOD MATRIX  (row = source, col = dest)");
    title(opsArea_, "OPERATORS");
    title(ampArea_, "AMP");

    // Matrix row/column headers (operator numbers).
    auto a = matrixArea_;
    a.removeFromTop(kSectionTitleH);
    auto headerRow = a.removeFromTop(kMatrixHeaderH);
    headerRow.removeFromLeft(kMatrixHeaderW);
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    g.setFont(FontManager::getInstance().getUIFontBold(9.0f));
    const int cw = headerRow.getWidth() / kNumOps;
    for (int dst = 0; dst < kNumOps; ++dst)
        g.drawText("->" + juce::String(dst + 1),
                   juce::Rectangle<int>(headerRow.getX() + dst * cw, headerRow.getY(), cw,
                                        headerRow.getHeight()),
                   juce::Justification::centred);
    auto rowsCol = a.removeFromLeft(kMatrixHeaderW);
    const int chh = rowsCol.getHeight() / kNumOps;
    for (int src = 0; src < kNumOps; ++src)
        g.drawText(juce::String(src + 1) + "->",
                   juce::Rectangle<int>(rowsCol.getX(), rowsCol.getY() + src * chh,
                                        rowsCol.getWidth(), chh),
                   juce::Justification::centred);
}

}  // namespace magda::daw::ui
