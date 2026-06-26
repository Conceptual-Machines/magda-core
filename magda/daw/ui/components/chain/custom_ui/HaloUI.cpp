#include "custom_ui/HaloUI.hpp"

#include <cmath>

#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {
const juce::Colour kBg{0xff0d0d0f};
const juce::Colour kPanel{0xff141417};
const juce::Colour kBorder{0xff242428};
const juce::Colour kText{0xffe4e4e8};
const juce::Colour kDim{0xff7a7a84};
const juce::Colour kAccent{0xff4f8fd6};
const juce::Colour kAccentFill{0xff2f6fb8};

const char* const kModelNames[6] = {"Modal", "Sympathetic", "String",
                                    "FM",    "Sym Quant",   "String+Verb"};
const char* const kPolyLabels[3] = {"1", "2", "4"};

bool isChord(int i) {
    return i == 6;
}
bool isPitch(int i) {
    return i == 7;
}
bool isFine(int i) {
    return i == 8;
}
bool isLevel(int i) {
    return i == 9;
}

void titleStrip(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                const juce::String& subtitle) {
    g.setColour(kText);
    g.setFont(FontManager::getInstance().getUIFontBold(10.5f));
    g.drawText(title, area.removeFromLeft(area.getWidth() / 2), juce::Justification::topLeft);
    g.setColour(kDim);
    g.setFont(FontManager::getInstance().getUIFont(9.5f));
    g.drawText(subtitle, area, juce::Justification::topRight);
}
}  // namespace

HaloUI::HaloUI() {
    auto add = [this](int idx, const juce::String& name) {
        auto& c = controls_[static_cast<size_t>(idx)];
        c.label = std::make_unique<juce::Label>();
        c.label->setText(name, juce::dontSendNotification);
        c.label->setColour(juce::Label::textColourId, kDim);
        c.label->setFont(FontManager::getInstance().getUIFont(11.0f));
        addAndMakeVisible(*c.label);

        c.slider = std::make_unique<LinkableTextSlider>(TextSlider::Format::Decimal);
        c.slider->setParamIndex(idx);
        c.slider->setTextColour(kText);
        if (isChord(idx)) {
            c.slider->setRange(0.0, 10.0, 1.0);
            c.slider->setValueFormatter([](double v) { return juce::String((int)std::lround(v)); });
        } else if (isPitch(idx)) {
            c.slider->setRange(-24.0, 24.0, 0.0);
            c.slider->setValueFormatter(
                [](double v) { return juce::String((int)std::lround(v)) + " st"; });
        } else if (isFine(idx)) {
            c.slider->setRange(-100.0, 100.0, 0.0);
            c.slider->setValueFormatter(
                [](double v) { return juce::String((int)std::lround(v)) + " ct"; });
        } else if (isLevel(idx)) {
            c.slider->setRange(-60.0, 12.0, 0.0);
            c.slider->setValueFormatter([](double v) { return juce::String(v, 1) + " dB"; });
        } else {
            c.slider->setRange(0.0, 1.0, 0.0);
            c.slider->setValueFormatter(
                [](double v) { return juce::String((int)std::lround(v * 100.0)) + "%"; });
        }
        c.slider->onValueChanged = [this, idx](double v) {
            if (onParameterChanged)
                onParameterChanged(idx, static_cast<float>(v));
            repaint();  // modal response tracks knob edits live
        };
        addAndMakeVisible(*c.slider);
    };

    add(kStructure, "Structure");
    add(kBrightness, "Brightness");
    add(kDamping, "Damping");
    add(kPosition, "Position");
    add(kChord, "Chord");
    add(kPitch, "Coarse");
    add(kFine, "Fine");
    add(kLevel, "Level");
    // kModel / kPolyphony are discrete, drawn as clickable segments below.
}

HaloUI::~HaloUI() = default;

void HaloUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    for (int i = 0; i < kNumParams; ++i) {
        if (i >= static_cast<int>(params.size()))
            break;
        const float value = params[static_cast<size_t>(i)].currentValue;
        if (i == kModel)
            curModel_ = juce::jlimit(0, kNumModels - 1, (int)std::lround(value));
        else if (i == kPolyphony)
            curPoly_ = juce::jlimit(0, kNumPoly - 1, (int)std::lround(value));
        else if (controls_[static_cast<size_t>(i)].slider != nullptr)
            controls_[static_cast<size_t>(i)].slider->setValue(value, juce::dontSendNotification);
    }
    repaint();
}

std::vector<LinkableTextSlider*> HaloUI::getLinkableSliders() {
    std::vector<LinkableTextSlider*> out;
    for (auto& c : controls_)
        if (c.slider != nullptr)
            out.push_back(c.slider.get());
    return out;
}

void HaloUI::paint(juce::Graphics& g) {
    g.fillAll(kBg);

    auto panel = [&](juce::Rectangle<int> r) {
        g.setColour(kPanel);
        g.fillRoundedRectangle(r.toFloat(), 6.0f);
        g.setColour(kBorder);
        g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 6.0f, 1.0f);
    };

    panel(resonatorArea_);
    titleStrip(g, resonatorArea_.reduced(14, 10), "RESONATOR", "modal - 64 partials - polyphonic");
    panel(paramsArea_);
    titleStrip(g, paramsArea_.reduced(14, 10), "PARAMETERS", "drag a value to set");
    panel(modelArea_);
    titleStrip(g, modelArea_.reduced(14, 10), "RESONATOR MODEL",
               juce::String(kModelNames[curModel_]).toUpperCase());

    // Modal response: partial amplitudes shaped by the resonator controls.
    if (modalVizArea_.getHeight() >= 40) {
        g.setColour(kBorder);
        g.drawRoundedRectangle(modalVizArea_.toFloat().reduced(0.5f), 4.0f, 1.0f);
        auto b = modalVizArea_.reduced(14, 22);
        const float structure = (float)controls_[kStructure].slider->getValue();
        const float brightness = (float)controls_[kBrightness].slider->getValue();
        const float damping = (float)controls_[kDamping].slider->getValue();
        const float position = (float)controls_[kPosition].slider->getValue();

        const float rolloff = juce::jmap(brightness, 0.0f, 1.0f, 0.80f, 0.985f);
        const float level = juce::jmap(damping, 0.0f, 1.0f, 0.5f, 1.0f);
        const int n = 48;
        const float bw = b.getWidth() / static_cast<float>(n);
        g.setColour(kAccent.withAlpha(0.75f));
        for (int i = 0; i < n; ++i) {
            const float harmonic = static_cast<float>(i + 1);
            const float comb = std::abs(
                std::sin(juce::MathConstants<float>::pi * harmonic * (0.02f + 0.48f * position)));
            const float inharm =
                0.5f + 0.5f * std::abs(std::sin(harmonic * (1.0f + 4.0f * structure)));
            const float amp = std::pow(rolloff, static_cast<float>(i)) * comb * inharm * level;
            const float h = b.getHeight() * juce::jlimit(0.0f, 1.0f, amp);
            const float x = b.getX() + i * bw + bw * 0.5f;
            g.fillRect(juce::Rectangle<float>(x - 0.9f, b.getBottom() - h, 1.8f, h));
        }
        g.setColour(kDim);
        g.setFont(FontManager::getInstance().getUIFont(9.5f));
        g.drawText("MODAL RESPONSE", modalVizArea_.reduced(12, 8), juce::Justification::topLeft);
        g.drawText("fund.", b.withTop(b.getBottom()).withHeight(14).withWidth(60),
                   juce::Justification::topLeft);
        g.drawText("partials ->", modalVizArea_.reduced(12, 8).removeFromBottom(14),
                   juce::Justification::bottomRight);
    }

    // Model selector: six clickable segments.
    g.setFont(FontManager::getInstance().getUIFontBold(10.0f));
    for (int i = 0; i < kNumModels; ++i) {
        const bool sel = i == curModel_;
        const auto r = modelBtn_[static_cast<size_t>(i)].toFloat();
        g.setColour(sel ? kAccentFill : kPanel.brighter(0.06f));
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(sel ? kAccent : kBorder);
        g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
        g.setColour(sel ? juce::Colours::white : kDim);
        g.drawText(kModelNames[i], modelBtn_[static_cast<size_t>(i)], juce::Justification::centred);
    }

    // Polyphony selector: 1 / 2 / 4 voices.
    g.setColour(kDim);
    g.setFont(FontManager::getInstance().getUIFontBold(9.0f));
    g.drawText("POLYPHONY", polyLabelRect_, juce::Justification::centredLeft);
    g.setColour(kAccent);
    g.drawText(juce::String(1 << curPoly_) + (curPoly_ == 0 ? " VOICE" : " VOICES"), polyLabelRect_,
               juce::Justification::centredRight);
    for (int i = 0; i < kNumPoly; ++i) {
        const bool sel = i == curPoly_;
        auto btn = polyBtn_[static_cast<size_t>(i)];  // local copy; removeFromBottom mutates
        g.setColour(sel ? kAccentFill : kPanel.brighter(0.06f));
        g.fillRoundedRectangle(btn.toFloat(), 4.0f);
        g.setColour(sel ? kAccent : kBorder);
        g.drawRoundedRectangle(btn.toFloat().reduced(0.5f), 4.0f, 1.0f);
        auto voicesRow = btn.removeFromBottom(13);  // btn is now just the number area
        g.setColour(sel ? juce::Colours::white : kDim);
        g.setFont(FontManager::getInstance().getUIFontBold(12.0f));
        g.drawText(kPolyLabels[i], btn, juce::Justification::centred);
        g.setColour(sel ? juce::Colours::white.withAlpha(0.7f) : kDim);
        g.setFont(FontManager::getInstance().getUIFont(8.0f));
        g.drawText(i == 0 ? "VOICE" : "VOICES", voicesRow, juce::Justification::centred);
    }
}

void HaloUI::mouseDown(const juce::MouseEvent& e) {
    const auto p = e.getPosition();
    for (int i = 0; i < kNumModels; ++i) {
        if (modelBtn_[static_cast<size_t>(i)].contains(p)) {
            curModel_ = i;
            if (onParameterChanged)
                onParameterChanged(kModel, static_cast<float>(i));
            repaint();
            return;
        }
    }
    for (int i = 0; i < kNumPoly; ++i) {
        if (polyBtn_[static_cast<size_t>(i)].contains(p)) {
            curPoly_ = i;
            if (onParameterChanged)
                onParameterChanged(kPolyphony, static_cast<float>(i));
            repaint();
            return;
        }
    }
}

void HaloUI::layoutRow(juce::Rectangle<int> row, const std::vector<int>& indices) {
    const int cellW = row.getWidth() / static_cast<int>(indices.size());
    for (int idx : indices) {
        auto cell = row.removeFromLeft(cellW).reduced(6, 0);
        auto& c = controls_[static_cast<size_t>(idx)];
        c.label->setBounds(cell.removeFromTop(14));
        c.slider->setBounds(cell.removeFromTop(30));
    }
}

void HaloUI::resized() {
    auto r = getLocalBounds().reduced(8);

    // RESONATOR spectrum on top (absorbs vertical slack).
    const int bottomH = 200;
    resonatorArea_ = r.removeFromTop(juce::jmax(140, r.getHeight() - bottomH - 8));
    r.removeFromTop(8);

    // Bottom split: PARAMETERS (left) | RESONATOR MODEL (right).
    paramsArea_ = r.removeFromLeft(r.getWidth() * 3 / 5 - 4);
    r.removeFromLeft(8);
    modelArea_ = r;

    {
        auto a = resonatorArea_.reduced(12, 10);
        a.removeFromTop(22);
        modalVizArea_ = a;
    }

    // PARAMETERS: two rows of four value boxes.
    {
        auto a = paramsArea_.reduced(12, 10);
        a.removeFromTop(22);
        const int rowH = 48;
        layoutRow(a.removeFromTop(rowH), {kStructure, kBrightness, kDamping, kPosition});
        a.removeFromTop(8);
        layoutRow(a.removeFromTop(rowH), {kChord, kPitch, kFine, kLevel});
    }

    // RESONATOR MODEL: 2x3 model buttons, then polyphony row.
    {
        auto a = modelArea_.reduced(12, 10);
        a.removeFromTop(22);
        auto modelGrid = a.removeFromTop(64);
        const int cols = 3, rows = 2;
        const int gx = 6, gy = 6;
        const int cw = (modelGrid.getWidth() - (cols - 1) * gx) / cols;
        const int ch = (modelGrid.getHeight() - (rows - 1) * gy) / rows;
        for (int i = 0; i < kNumModels; ++i) {
            const int row = i / cols, col = i % cols;
            modelBtn_[static_cast<size_t>(i)] = juce::Rectangle<int>(
                modelGrid.getX() + col * (cw + gx), modelGrid.getY() + row * (ch + gy), cw, ch);
        }

        a.removeFromTop(12);
        polyLabelRect_ = a.removeFromTop(16);
        a.removeFromTop(4);
        auto polyRow = a.removeFromTop(40);
        const int pw = (polyRow.getWidth() - 2 * gx) / kNumPoly;
        for (int i = 0; i < kNumPoly; ++i)
            polyBtn_[static_cast<size_t>(i)] = juce::Rectangle<int>(
                polyRow.getX() + i * (pw + gx), polyRow.getY(), pw, polyRow.getHeight());
    }
}

}  // namespace magda::daw::ui
