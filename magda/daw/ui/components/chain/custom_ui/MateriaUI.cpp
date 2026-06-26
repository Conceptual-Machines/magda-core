#include "custom_ui/MateriaUI.hpp"

#include <cmath>

#include "ui/themes/FontManager.hpp"

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
    g.setFont(FontManager::getInstance().getUIFontBold(10.5f));
    g.drawText(title, area.removeFromLeft(area.getWidth() / 2), juce::Justification::topLeft);
    g.setColour(kDim);
    g.setFont(FontManager::getInstance().getUIFont(9.5f));
    g.drawText(subtitle, area, juce::Justification::topRight);
}
}  // namespace

MateriaUI::MateriaUI() {
    auto add = [this](int idx, const juce::String& name, juce::Colour accent) {
        auto& c = controls_[static_cast<size_t>(idx)];
        c.label = std::make_unique<juce::Label>();
        c.label->setText(name, juce::dontSendNotification);
        c.label->setColour(juce::Label::textColourId, kDim);
        c.label->setFont(FontManager::getInstance().getUIFont(11.0f));
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
            repaint();  // blend dot + modal response track knob edits live
        };
        addAndMakeVisible(*c.slider);
    };

    add(kContour, "Contour", kText);
    add(kSignature, "Signature", kText);
    add(kPitch, "Coarse", kText);
    add(kFine, "Fine", kText);
    add(kLevel, "Level", kText);
    add(kVelAmp, "Vel>Amp", kText);

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
    repaint();  // refresh blend dot + modal response from live values
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
    {
        auto t = voiceArea_.reduced(14, 8);
        g.setColour(kText);
        g.setFont(FontManager::getInstance().getUIFontBold(10.5f));
        g.drawText("VOICE", t.removeFromTop(14), juce::Justification::topLeft);

        // Segment the value boxes into named zones with vertical dividers.
        // Groups: [Contour, Signature] | [Coarse, Fine] | [Level].
        const auto z = voiceZonesArea_;
        const int cellW = z.getWidth() / 6;
        g.setColour(kBorder);
        g.fillRect(z.getX() + 2 * cellW, z.getY(), 1, z.getHeight());
        g.fillRect(z.getX() + 4 * cellW, z.getY(), 1, z.getHeight());

        g.setColour(kDim);
        g.setFont(FontManager::getInstance().getUIFontBold(9.0f));
        auto caption = [&](const juce::String& s, int startCell, int cells) {
            g.drawText(
                s, juce::Rectangle<int>(z.getX() + startCell * cellW, z.getY(), cells * cellW, 13),
                juce::Justification::centred);
        };
        caption("EXCITATION CONTOUR", 0, 2);
        caption("PITCH", 2, 2);
        caption("OUTPUT", 4, 2);
    }
    panel(exciterArea_);
    titleStrip(g, exciterArea_.reduced(14, 10), "EXCITER", "");
    {
        // Label the bow / blow / strike columns in their accent colours, with
        // vertical dividers between them.
        const auto z = exciterColsArea_;
        const int colW = z.getWidth() / 3;
        g.setColour(kBorder);
        g.fillRect(z.getX() + colW, z.getY(), 1, z.getHeight());
        g.fillRect(z.getX() + 2 * colW, z.getY(), 1, z.getHeight());
        g.setFont(FontManager::getInstance().getUIFontBold(9.0f));
        auto cap = [&](const juce::String& s, int col, juce::Colour c) {
            g.setColour(c);
            g.drawText(s, juce::Rectangle<int>(z.getX() + col * colW, z.getY(), colW, 13),
                       juce::Justification::centred);
        };
        cap("BOW", 0, cBow);
        cap("BLOW", 1, cBlow);
        cap("STRIKE", 2, cStrike);
    }
    panel(resonatorArea_);
    titleStrip(g, resonatorArea_.reduced(14, 10), "RESONATOR", "modal - 64 partials");

    // Excitation-blend triangle: the dot is the barycentre of the live
    // bow/blow/strike levels; drag it to set the three at once.
    g.setColour(kBorder);
    g.drawRoundedRectangle(blendArea_.toFloat().reduced(0.5f), 4.0f, 1.0f);
    {
        const auto c = triangleCorners();
        const auto& bow = c[0];
        const auto& blow = c[1];
        const auto& strike = c[2];
        g.setColour(kDim.withAlpha(0.5f));
        g.drawLine({bow, blow}, 1.0f);
        g.drawLine({bow, strike}, 1.0f);
        g.drawLine({blow, strike}, 1.0f);
        g.setColour(cBow);
        g.fillEllipse(juce::Rectangle<float>(8, 8).withCentre(bow));
        g.setColour(cBlow);
        g.fillEllipse(juce::Rectangle<float>(8, 8).withCentre(blow));
        g.setColour(cStrike);
        g.fillEllipse(juce::Rectangle<float>(8, 8).withCentre(strike));

        const float wBow = (float)controls_[kBow].slider->getValue();
        const float wBlow = (float)controls_[kBlow].slider->getValue();
        const float wStrike = (float)controls_[kStrike].slider->getValue();
        const float sum = wBow + wBlow + wStrike;
        juce::Point<float> dot = sum > 1.0e-4f
                                     ? (bow * wBow + blow * wBlow + strike * wStrike) / sum
                                     : (bow + blow + strike) / 3.0f;
        const juce::Colour dotCol = sum > 1.0e-4f ? cBow.withMultipliedAlpha(wBow / sum)
                                                        .interpolatedWith(cBlow, wBlow / sum)
                                                        .interpolatedWith(cStrike, wStrike / sum)
                                                  : kText;
        g.setColour(kBg);
        g.fillEllipse(juce::Rectangle<float>(14, 14).withCentre(dot));
        g.setColour(dotCol.withAlpha(1.0f));
        g.fillEllipse(juce::Rectangle<float>(11, 11).withCentre(dot));
        g.setColour(kText);
        g.drawEllipse(juce::Rectangle<float>(11, 11).withCentre(dot), 1.2f);

        g.setColour(kDim);
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.drawText("EXCITATION BLEND", blendArea_.reduced(10, 8), juce::Justification::topLeft);
    }

    // Modal response: partial amplitudes shaped by the resonator controls.
    // Brightness tilts the spectrum, Position combs it, Geometry adds the
    // inharmonic structure, Damping sets the overall level.
    if (modalVizArea_.getHeight() >= 40) {
        g.setColour(kBorder);
        g.drawRoundedRectangle(modalVizArea_.toFloat().reduced(0.5f), 4.0f, 1.0f);
        auto b = modalVizArea_.reduced(12, 16);
        const float geometry = (float)controls_[kGeometry].slider->getValue();
        const float brightness = (float)controls_[kBrightness].slider->getValue();
        const float damping = (float)controls_[kDamping].slider->getValue();
        const float position = (float)controls_[kPosition].slider->getValue();

        const float rolloff = juce::jmap(brightness, 0.0f, 1.0f, 0.80f, 0.985f);
        const float level = juce::jmap(damping, 0.0f, 1.0f, 0.45f, 1.0f);
        const int n = 40;
        const float bw = b.getWidth() / static_cast<float>(n);
        g.setColour(kReso.withAlpha(0.6f));
        for (int i = 0; i < n; ++i) {
            const float harmonic = static_cast<float>(i + 1);
            const float comb = std::abs(
                std::sin(juce::MathConstants<float>::pi * harmonic * (0.02f + 0.48f * position)));
            const float structure =
                0.55f + 0.45f * std::abs(std::sin(harmonic * (1.0f + 3.0f * geometry)));
            const float amp = std::pow(rolloff, static_cast<float>(i)) * comb * structure * level;
            const float h = b.getHeight() * juce::jlimit(0.0f, 1.0f, amp);
            g.fillRect(juce::Rectangle<float>(b.getX() + i * bw + 1, b.getBottom() - h, bw - 2, h));
        }
        g.setColour(kDim);
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.drawText("modal response", modalVizArea_.reduced(10, 8), juce::Justification::topRight);
    }
}

std::array<juce::Point<float>, 3> MateriaUI::triangleCorners() const {
    auto b = blendArea_.reduced(28);
    return {juce::Point<float>(b.getCentreX(), (float)b.getY()),             // bow   (top)
            juce::Point<float>((float)b.getX(), (float)b.getBottom()),       // blow  (bottom-left)
            juce::Point<float>((float)b.getRight(), (float)b.getBottom())};  // strike(bottom-right)
}

void MateriaUI::applyBlendDrag(juce::Point<int> p) {
    const auto c = triangleCorners();
    const auto A = c[0], B = c[1], C = c[2];  // bow, blow, strike
    const auto P = p.toFloat();

    // Barycentric weights of P within triangle ABC.
    const auto v0 = B - A, v1 = C - A, v2 = P - A;
    const float d00 = v0.x * v0.x + v0.y * v0.y;
    const float d01 = v0.x * v1.x + v0.y * v1.y;
    const float d11 = v1.x * v1.x + v1.y * v1.y;
    const float d20 = v2.x * v0.x + v2.y * v0.y;
    const float d21 = v2.x * v1.x + v2.y * v1.y;
    const float denom = d00 * d11 - d01 * d01;
    if (std::abs(denom) < 1.0e-6f)
        return;
    float beta = (d11 * d20 - d01 * d21) / denom;   // blow
    float gamma = (d00 * d21 - d01 * d20) / denom;  // strike
    float alpha = 1.0f - beta - gamma;              // bow

    alpha = juce::jlimit(0.0f, 1.0f, alpha);
    beta = juce::jlimit(0.0f, 1.0f, beta);
    gamma = juce::jlimit(0.0f, 1.0f, gamma);

    auto set = [this](int idx, float v) {
        controls_[static_cast<size_t>(idx)].slider->setValue(v, juce::dontSendNotification);
        if (onParameterChanged)
            onParameterChanged(idx, v);
    };
    set(kBow, alpha);
    set(kBlow, beta);
    set(kStrike, gamma);
    repaint();
}

void MateriaUI::mouseDown(const juce::MouseEvent& e) {
    if (blendArea_.contains(e.getPosition()))
        applyBlendDrag(e.getPosition());
}

void MateriaUI::mouseDrag(const juce::MouseEvent& e) {
    if (blendArea_.contains(e.getMouseDownPosition()))
        applyBlendDrag(e.getPosition());
}

void MateriaUI::layoutRows(juce::Rectangle<int> area, const std::vector<int>& indices, int rowH,
                           int gap) {
    for (int idx : indices) {
        auto row = area.removeFromTop(rowH);
        auto& c = controls_[static_cast<size_t>(idx)];
        c.label->setBounds(row.removeFromTop(14));
        c.slider->setBounds(row.reduced(0, 1));
        area.removeFromTop(gap);
    }
}

void MateriaUI::resized() {
    // The chain row stretches us to the full panel height, so the layout is
    // height-elastic: the two visualisations absorb the slack while the control
    // stacks keep a fixed size and sit centred in their columns.
    constexpr int kRowH = 44;
    constexpr int kGap = 8;

    auto r = getLocalBounds().reduced(8);

    voiceArea_ = r.removeFromTop(78);
    r.removeFromTop(8);

    auto body = r;
    exciterArea_ = body.removeFromLeft(body.getWidth() / 2 - 4);
    body.removeFromLeft(8);
    resonatorArea_ = body;

    // VOICE: zone-captioned value boxes in a row (caption strip + boxes form the
    // segmented band painted with dividers).
    {
        auto a = voiceArea_.reduced(14, 8);
        a.removeFromTop(14);  // title strip
        voiceZonesArea_ = a;
        a.removeFromTop(13);  // zone-caption strip
        const std::array<int, 6> order{kContour, kSignature, kPitch, kFine, kLevel, kVelAmp};
        int cellW = a.getWidth() / 6;
        for (int k = 0; k < 6; ++k) {
            auto cell = a.removeFromLeft(cellW).reduced(6, 0);
            auto& c = controls_[static_cast<size_t>(order[static_cast<size_t>(k)])];
            c.label->setBounds(cell.removeFromTop(12));
            c.slider->setBounds(cell.removeFromTop(26));
        }
    }

    // Centre a stack of `rows` rows (rowH + gap each) within `col`, keeping a
    // common top across columns so the grid stays aligned.
    auto rowStack = [](juce::Rectangle<int> col, int rows) {
        const int stackH = rows * kRowH + (rows - 1) * kGap;
        const int top = col.getY() + juce::jmax(0, (col.getHeight() - stackH) / 2);
        return juce::Rectangle<int>(col.getX(), top, col.getWidth(), stackH);
    };

    // EXCITER: blend pad on the LEFT, Bow/Blow/Strike columns beside it. The pad
    // fills the full height; the columns centre their rows against a 3-row grid.
    {
        auto a = exciterArea_.reduced(12, 10);
        a.removeFromTop(22);
        blendArea_ = a.removeFromLeft(juce::jmax(120, a.getWidth() * 4 / 9));
        a.removeFromLeft(10);
        exciterColsArea_ = a;  // caption strip + the three columns
        a.removeFromTop(15);   // column-label strip
        int colW = a.getWidth() / 3;
        bowCol_ = a.removeFromLeft(colW).reduced(3, 0);
        blowCol_ = a.removeFromLeft(colW).reduced(3, 0);
        strikeCol_ = a.reduced(3, 0);
        layoutRows(rowStack(bowCol_, 3), {kBow, kBowTimbre}, kRowH, kGap);
        layoutRows(rowStack(blowCol_, 3), {kBlow, kBlowFlow, kBlowTimbre}, kRowH, kGap);
        layoutRows(rowStack(strikeCol_, 3), {kStrike, kStrikeMallet, kStrikeTimbre}, kRowH, kGap);
    }

    // RESONATOR: modal-response viz on top (absorbs the slack), five sliders as
    // a 3-then-2 grid below (always visible).
    {
        auto a = resonatorArea_.reduced(12, 10);
        a.removeFromTop(22);
        const int controlsH = 2 * kRowH + kGap;
        auto ctrl = a.removeFromBottom(controlsH);
        a.removeFromBottom(10);
        modalVizArea_ = a;  // whatever is left on top; the viz flexes with panel height

        const int colW = ctrl.getWidth() / 3;
        auto cell = [&](juce::Rectangle<int> row, int col, int idx) {
            auto box = row.withX(row.getX() + col * colW).withWidth(colW).reduced(4, 0);
            auto& c = controls_[static_cast<size_t>(idx)];
            c.label->setBounds(box.removeFromTop(16));
            c.slider->setBounds(box.removeFromTop(30));
        };
        auto row1 = ctrl.removeFromTop(kRowH);
        ctrl.removeFromTop(kGap);
        auto row2 = ctrl;
        cell(row1, 0, kGeometry);
        cell(row1, 1, kBrightness);
        cell(row1, 2, kDamping);
        cell(row2, 0, kPosition);
        cell(row2, 1, kSpace);
    }
}

}  // namespace magda::daw::ui
