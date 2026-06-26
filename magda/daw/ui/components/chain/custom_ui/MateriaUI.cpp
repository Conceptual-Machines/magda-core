#include "custom_ui/MateriaUI.hpp"

#include <cmath>

namespace magda::daw::ui {

namespace {
const juce::Colour kBg{0xff0d0d0f};
const juce::Colour kPanel{0xff141417};
const juce::Colour kBorder{0xff242428};
const juce::Colour kText{0xffe4e4e8};
const juce::Colour kDim{0xff7a7a84};
const juce::Colour cBow{0xffc9c9d0};
const juce::Colour cBlow{0xffe0556f};
const juce::Colour cStrike{0xff45c8d0};
const juce::Colour kReso{0xff5b8fd0};

bool isPitch(int i) {
    return i == 15;
}
bool isFine(int i) {
    return i == 16;
}
bool isLevel(int i) {
    return i == 17;
}

void titleStrip(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                const juce::String& subtitle) {
    g.setColour(kText);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).boldened());
    g.drawText(title, area.removeFromLeft(area.getWidth() / 2), juce::Justification::topLeft);
    g.setColour(kDim);
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText(subtitle, area, juce::Justification::topRight);
}
}  // namespace

MateriaUI::MateriaUI() {
    auto add = [this](int idx, const juce::String& name, juce::Colour accent) {
        auto& c = controls_[static_cast<size_t>(idx)];
        c.label = std::make_unique<juce::Label>();
        c.label->setText(name, juce::dontSendNotification);
        c.label->setColour(juce::Label::textColourId, kDim);
        c.label->setFont(juce::Font(juce::FontOptions(11.0f)));
        addAndMakeVisible(*c.label);

        c.slider = std::make_unique<LinkableTextSlider>(TextSlider::Format::Decimal);
        c.slider->setParamIndex(idx);
        c.slider->setTextColour(accent);
        if (isPitch(idx)) {
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
        };
        addAndMakeVisible(*c.slider);
    };

    add(kContour, "Contour", kText);
    add(kSignature, "Signature", kText);
    add(kPitch, "Coarse", kText);
    add(kFine, "Fine", kText);
    add(kLevel, "Level", kText);

    add(kBow, "Level", cBow);
    add(kBowTimbre, "Timbre", cBow);
    add(kBlow, "Level", cBlow);
    add(kBlowFlow, "Flow", cBlow);
    add(kBlowTimbre, "Timbre", cBlow);
    add(kStrike, "Level", cStrike);
    add(kStrikeMallet, "Mallet", cStrike);
    add(kStrikeTimbre, "Timbre", cStrike);

    add(kGeometry, "Geometry", kReso);
    add(kBrightness, "Brightness", kReso);
    add(kDamping, "Damping", kReso);
    add(kPosition, "Position", kReso);
    add(kSpace, "Space", kReso);
}

MateriaUI::~MateriaUI() = default;

void MateriaUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    for (int i = 0; i < kNumParams; ++i) {
        auto& c = controls_[static_cast<size_t>(i)];
        if (c.slider == nullptr)
            continue;
        if (i < static_cast<int>(params.size()))
            c.slider->setValue(params[static_cast<size_t>(i)].currentValue,
                               juce::dontSendNotification);
    }
}

std::vector<LinkableTextSlider*> MateriaUI::getLinkableSliders() {
    std::vector<LinkableTextSlider*> out;
    for (auto& c : controls_)
        if (c.slider != nullptr)
            out.push_back(c.slider.get());
    return out;
}

void MateriaUI::paint(juce::Graphics& g) {
    g.fillAll(kBg);

    auto panel = [&](juce::Rectangle<int> r) {
        g.setColour(kPanel);
        g.fillRoundedRectangle(r.toFloat(), 6.0f);
        g.setColour(kBorder);
        g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 6.0f, 1.0f);
    };

    panel(voiceArea_);
    titleStrip(g, voiceArea_.reduced(14, 10), "VOICE", "excitation contour - pitch - output");
    panel(exciterArea_);
    titleStrip(g, exciterArea_.reduced(14, 10), "EXCITER", "bow - blow - strike");
    panel(resonatorArea_);
    titleStrip(g, resonatorArea_.reduced(14, 10), "RESONATOR", "modal - 64 partials");

    // Excitation-blend triangle placeholder.
    g.setColour(kBorder);
    g.drawRoundedRectangle(blendArea_.toFloat().reduced(0.5f), 4.0f, 1.0f);
    {
        auto b = blendArea_.reduced(28);
        juce::Point<float> bow(b.getCentreX(), (float)b.getY());
        juce::Point<float> blow((float)b.getX(), (float)b.getBottom());
        juce::Point<float> strike((float)b.getRight(), (float)b.getBottom());
        g.setColour(kDim.withAlpha(0.5f));
        g.drawLine({bow, blow}, 1.0f);
        g.drawLine({bow, strike}, 1.0f);
        g.drawLine({blow, strike}, 1.0f);
        g.setColour(cBlow);
        g.fillEllipse(juce::Rectangle<float>(8, 8).withCentre(blow));
        g.setColour(cStrike);
        g.fillEllipse(juce::Rectangle<float>(8, 8).withCentre(strike));
        g.setColour(kText);
        g.fillEllipse(juce::Rectangle<float>(10, 10).withCentre(b.getCentre().toFloat()));
        g.setColour(kDim);
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText("EXCITATION BLEND", blendArea_.reduced(10, 8), juce::Justification::topLeft);
    }

    // Modal-response placeholder: faint decaying partials.
    g.setColour(kBorder);
    g.drawRoundedRectangle(modalVizArea_.toFloat().reduced(0.5f), 4.0f, 1.0f);
    {
        auto b = modalVizArea_.reduced(12, 16);
        const int n = 40;
        const float bw = b.getWidth() / static_cast<float>(n);
        g.setColour(kReso.withAlpha(0.55f));
        for (int i = 0; i < n; ++i) {
            float decay = std::exp(-static_cast<float>(i) * 0.12f);
            float h = b.getHeight() * decay * (0.4f + 0.6f * std::abs(std::sin(i * 1.7f)));
            g.fillRect(juce::Rectangle<float>(b.getX() + i * bw + 1, b.getBottom() - h, bw - 2, h));
        }
        g.setColour(kDim);
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText("modal response", modalVizArea_.reduced(10, 8), juce::Justification::topRight);
    }

    // Level meter placeholder (vertical gradient strip).
    juce::ColourGradient meter(juce::Colour(0xff4caf50), meterArea_.getX(),
                               (float)meterArea_.getBottom(), juce::Colour(0xfff44336),
                               meterArea_.getX(), (float)meterArea_.getY(), false);
    meter.addColour(0.5, juce::Colour(0xffffc107));
    g.setGradientFill(meter);
    g.fillRoundedRectangle(meterArea_.toFloat(), 2.0f);
}

void MateriaUI::layoutRows(juce::Rectangle<int> area, const std::vector<int>& indices, int rowH) {
    for (int idx : indices) {
        auto row = area.removeFromTop(rowH);
        auto& c = controls_[static_cast<size_t>(idx)];
        auto top = row.removeFromTop(16);
        c.label->setBounds(top.removeFromLeft(top.getWidth() / 2));
        c.slider->setBounds(row.reduced(0, 1));
        area.removeFromTop(6);
    }
}

void MateriaUI::resized() {
    auto r = getLocalBounds().reduced(8);

    voiceArea_ = r.removeFromTop(96);
    r.removeFromTop(8);

    auto body = r;
    exciterArea_ = body.removeFromLeft(body.getWidth() / 2 - 4);
    body.removeFromLeft(8);
    resonatorArea_ = body;

    // VOICE: five value boxes in a row (Contour, Signature, Coarse, Fine, Level).
    {
        auto a = voiceArea_.reduced(14, 12);
        a.removeFromTop(22);  // title strip
        const std::array<int, 5> order{kContour, kSignature, kPitch, kFine, kLevel};
        int cellW = a.getWidth() / 5;
        for (int k = 0; k < 5; ++k) {
            auto cell = a.removeFromLeft(cellW).reduced(6, 0);
            auto& c = controls_[static_cast<size_t>(order[static_cast<size_t>(k)])];
            c.label->setBounds(cell.removeFromTop(16));
            c.slider->setBounds(cell.removeFromTop(28));
        }
    }

    // EXCITER: blend pad on top, then three columns.
    {
        auto a = exciterArea_.reduced(14, 12);
        a.removeFromTop(24);
        blendArea_ = a.removeFromTop(juce::jmax(120, a.getHeight() / 2));
        a.removeFromTop(10);
        int colW = a.getWidth() / 3;
        bowCol_ = a.removeFromLeft(colW).reduced(4, 0);
        blowCol_ = a.removeFromLeft(colW).reduced(4, 0);
        strikeCol_ = a.reduced(4, 0);
        layoutRows(bowCol_.withTrimmedTop(20), {kBow, kBowTimbre}, 34);
        layoutRows(blowCol_.withTrimmedTop(20), {kBlow, kBlowFlow, kBlowTimbre}, 34);
        layoutRows(strikeCol_.withTrimmedTop(20), {kStrike, kStrikeMallet, kStrikeTimbre}, 34);
    }

    // RESONATOR: modal viz + meter on top, then five rows.
    {
        auto a = resonatorArea_.reduced(14, 12);
        a.removeFromTop(24);
        meterArea_ = a.removeFromRight(8);
        a.removeFromRight(10);
        modalVizArea_ = a.removeFromTop(juce::jmax(110, a.getHeight() / 2));
        a.removeFromTop(10);
        layoutRows(a, {kGeometry, kBrightness, kDamping, kPosition, kSpace}, 34);
    }
}

}  // namespace magda::daw::ui
