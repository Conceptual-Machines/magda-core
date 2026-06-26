#include "custom_ui/NimbusUI.hpp"

#include <cmath>

#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {
const juce::Colour kBg{0xff0d0d0f};
const juce::Colour kPanel{0xff141417};
const juce::Colour kBorder{0xff242428};
const juce::Colour kText{0xffe4e4e8};
const juce::Colour kDim{0xff7a7a84};
const juce::Colour kCyan{0xff45c8d0};
const juce::Colour kPink{0xffe0556f};
const juce::Colour kBlue{0xff4f8fd6};
const juce::Colour kGrain{0xfff2f6ff};

struct ModeDesc {
    const char* name;
    const char* sub;
};
const ModeDesc kModes[4] = {{"GRANULAR", "classic grains"},
                            {"STRETCH", "pitch - time"},
                            {"DELAY", "looping echo"},
                            {"SPECTRAL", "fft clouds"}};

bool isPitch(int i) {
    return i == 2;
}

// Stable pseudo-random in [0,1) for grain-cloud particle placement.
float hash1(int i) {
    float s = std::sin(static_cast<float>(i) * 12.9898f) * 43758.5453f;
    return s - std::floor(s);
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

NimbusUI::NimbusUI() {
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
        if (isPitch(idx)) {
            c.slider->setRange(-24.0, 24.0, 0.0);
            c.slider->setValueFormatter([](double v) { return juce::String(v, 1) + " st"; });
        } else {
            c.slider->setRange(0.0, 1.0, 0.0);
            c.slider->setValueFormatter(
                [](double v) { return juce::String((int)std::lround(v * 100.0)) + "%"; });
        }
        c.slider->onValueChanged = [this, idx](double v) {
            if (onParameterChanged)
                onParameterChanged(idx, static_cast<float>(v));
            repaint();
        };
        addAndMakeVisible(*c.slider);
    };

    add(kPosition, "Position");
    add(kSize, "Size");
    add(kPitch, "Pitch");
    add(kDensity, "Density");
    add(kTexture, "Texture");
    add(kDryWet, "Dry/Wet");
    add(kSpread, "Spread");
    add(kFeedback, "Feedback");
    add(kReverb, "Reverb");
    // kMode / kFreeze are discrete, drawn as clickable segments.

    // Generate a synthetic record-buffer envelope (sines + value noise).
    juce::Random rng(0x6c10d5);
    for (int i = 0; i < kWaveN; ++i) {
        const float t = static_cast<float>(i) / kWaveN;
        float env = 0.35f + 0.65f * std::abs(std::sin(t * 6.2831f * 3.0f) * 0.6f +
                                             std::sin(t * 6.2831f * 7.3f) * 0.3f);
        float n = (rng.nextFloat() * 2.0f - 1.0f) * 0.5f;
        float amp = juce::jlimit(0.05f, 1.0f, env * (0.7f + 0.3f * std::abs(n)));
        waveHi_[static_cast<size_t>(i)] = amp;
        waveLo_[static_cast<size_t>(i)] = -amp * (0.8f + 0.2f * std::abs(n));
    }

    startTimerHz(30);
}

NimbusUI::~NimbusUI() {
    stopTimer();
}

void NimbusUI::timerCallback() {
    animPhase_ += 0.09f;
    if (!bufferVizArea_.isEmpty())
        repaint(bufferVizArea_);
}

void NimbusUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    for (int i = 0; i < kNumParams; ++i) {
        if (i >= static_cast<int>(params.size()))
            break;
        const float value = params[static_cast<size_t>(i)].currentValue;
        if (i == kMode)
            curMode_ = juce::jlimit(0, kNumModes - 1, (int)std::lround(value));
        else if (i == kFreeze)
            freeze_ = value > 0.5f;
        else if (controls_[static_cast<size_t>(i)].slider != nullptr)
            controls_[static_cast<size_t>(i)].slider->setValue(value, juce::dontSendNotification);
    }
    repaint();
}

std::vector<LinkableTextSlider*> NimbusUI::getLinkableSliders() {
    std::vector<LinkableTextSlider*> out;
    for (auto& c : controls_)
        if (c.slider != nullptr)
            out.push_back(c.slider.get());
    return out;
}

void NimbusUI::paintBuffer(juce::Graphics& g) {
    auto b = bufferVizArea_;
    g.setColour(juce::Colour{0xff0a1416});
    g.fillRoundedRectangle(b.toFloat(), 4.0f);
    g.setColour(kBorder);
    g.drawRoundedRectangle(b.toFloat().reduced(0.5f), 4.0f, 1.0f);

    auto plot = b.reduced(10, 18);
    const float cy = plot.getCentreY();
    const float midY = plot.getY() + plot.getHeight() * 0.5f;

    // Centre line.
    g.setColour(kBorder.withAlpha(0.6f));
    g.drawHorizontalLine((int)midY, (float)plot.getX(), (float)plot.getRight());

    // Waveform envelope.
    const float colW = plot.getWidth() / static_cast<float>(kWaveN);
    const float half = plot.getHeight() * 0.46f;
    g.setColour(kCyan.withAlpha(0.55f));
    for (int i = 0; i < kWaveN; ++i) {
        const float x = plot.getX() + i * colW;
        const float hi = cy - waveHi_[static_cast<size_t>(i)] * half;
        const float lo = cy - waveLo_[static_cast<size_t>(i)] * half;
        g.fillRect(juce::Rectangle<float>(x, hi, juce::jmax(0.6f, colW - 0.4f), lo - hi));
    }

    // Grain window (driven by Position / Size), and the grain cloud.
    const float position = (float)controls_[kPosition].slider->getValue();
    const float size = (float)controls_[kSize].slider->getValue();
    const float density = (float)controls_[kDensity].slider->getValue();
    const float texture = (float)controls_[kTexture].slider->getValue();

    const float winW =
        juce::jlimit(14.0f, (float)plot.getWidth(), (0.06f + 0.30f * size) * plot.getWidth());
    float winCx = plot.getX() + position * plot.getWidth();
    winCx = juce::jlimit(plot.getX() + winW * 0.5f, plot.getRight() - winW * 0.5f, winCx);
    juce::Rectangle<float> win(winCx - winW * 0.5f, (float)plot.getY(), winW,
                               (float)plot.getHeight());

    g.setColour(kCyan.withAlpha(freeze_ ? 0.18f : 0.10f));
    g.fillRect(win);
    g.setColour(kCyan.withAlpha(0.7f));
    g.fillRect(win.getX(), win.getY(), 1.5f, win.getHeight());
    g.fillRect(win.getRight() - 1.5f, win.getY(), 1.5f, win.getHeight());

    // Grain cloud particles.
    const int n = 10 + (int)std::lround(density * 70.0f);
    for (int k = 0; k < n; ++k) {
        const float bx = hash1(k * 2 + 1);
        const float by = hash1(k * 2 + 2) * 2.0f - 1.0f;
        const float jitter = 1.0f + 3.0f * texture;
        const float px =
            win.getX() + bx * win.getWidth() + std::sin(animPhase_ * 1.3f + k * 1.7f) * jitter;
        const float py =
            midY + by * half * (0.25f + 0.7f * texture) + std::cos(animPhase_ + k * 2.3f) * jitter;
        const float a = 0.25f + 0.55f * std::abs(std::sin(animPhase_ + k * 0.9f));
        const float rad = 1.4f + 1.6f * hash1(k * 2 + 5);
        g.setColour(kGrain.withAlpha(a * 0.35f));
        g.fillEllipse(px - rad * 2.0f, py - rad * 2.0f, rad * 4.0f, rad * 4.0f);  // glow
        g.setColour(kGrain.withAlpha(a));
        g.fillEllipse(px - rad, py - rad, rad * 2.0f, rad * 2.0f);
    }

    // Playhead at the window centre.
    g.setColour(kPink);
    g.fillRect(winCx - 0.75f, (float)plot.getY() - 6.0f, 1.5f, (float)plot.getHeight() + 6.0f);
    g.fillEllipse(winCx - 3.0f, (float)plot.getY() - 9.0f, 6.0f, 6.0f);

    // Labels.
    g.setColour(kDim);
    g.setFont(FontManager::getInstance().getUIFontBold(9.0f));
    g.drawText("RECORD BUFFER", b.reduced(12, 8), juce::Justification::topLeft);
    g.setFont(FontManager::getInstance().getUIFont(9.0f));
    g.drawText("-8s", b.reduced(12, 8), juce::Justification::bottomLeft);
    g.drawText("now", b.reduced(12, 8), juce::Justification::bottomRight);

    // Freeze / mode indicator (top-right).
    auto ind = b.reduced(12, 8).removeFromTop(14);
    g.setFont(FontManager::getInstance().getUIFontBold(9.0f));
    g.setColour(kDim);
    g.drawText(juce::String(kModes[curMode_].name), ind, juce::Justification::topRight);
    auto frozen = ind.withTrimmedRight(74);
    g.setColour(freeze_ ? kCyan : kDim.withAlpha(0.6f));
    g.drawText(juce::String::fromUTF8("\xe2\x97\x8f ") + "FROZEN", frozen,
               juce::Justification::topRight);
}

void NimbusUI::paint(juce::Graphics& g) {
    g.fillAll(kBg);

    auto panel = [&](juce::Rectangle<int> r) {
        g.setColour(kPanel);
        g.fillRoundedRectangle(r.toFloat(), 6.0f);
        g.setColour(kBorder);
        g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 6.0f, 1.0f);
    };

    panel(grainArea_);
    titleStrip(g, grainArea_.reduced(14, 10), "GRAIN BUFFER", "8s stereo - granular cloud");
    paintBuffer(g);

    panel(paramsArea_);
    titleStrip(g, paramsArea_.reduced(14, 10), "PARAMETERS", "drag a value to set");

    panel(ctrlArea_);

    // FREEZE toggle.
    {
        const auto r = freezeBtn_.toFloat();
        g.setColour(freeze_ ? kCyan.withAlpha(0.18f) : kPanel.brighter(0.05f));
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(freeze_ ? kCyan : kBorder);
        g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, freeze_ ? 1.5f : 1.0f);
        auto lr = freezeBtn_.reduced(16, 0);
        const float dotR = 7.0f;
        juce::Rectangle<float> dot(lr.getX(), lr.getCentreY() - dotR, dotR * 2.0f, dotR * 2.0f);
        g.setColour(freeze_ ? kCyan : kDim);
        if (freeze_)
            g.fillEllipse(dot);
        else
            g.drawEllipse(dot.reduced(1.0f), 1.5f);
        g.setColour(freeze_ ? kText : kDim);
        g.setFont(FontManager::getInstance().getUIFontBold(14.0f));
        g.drawText("FREEZE", lr.withTrimmedLeft(26), juce::Justification::centredLeft);
        g.setColour(kDim);
        g.setFont(FontManager::getInstance().getUIFont(9.0f));
        g.drawText("hold buffer", lr, juce::Justification::centredRight);
    }

    // PLAYBACK MODE label + current mode.
    g.setColour(kDim);
    g.setFont(FontManager::getInstance().getUIFontBold(9.0f));
    g.drawText("PLAYBACK MODE", modeLabelRect_, juce::Justification::centredLeft);
    g.setColour(kCyan);
    g.drawText(juce::String(kModes[curMode_].name), modeLabelRect_,
               juce::Justification::centredRight);

    for (int i = 0; i < kNumModes; ++i) {
        const bool sel = i == curMode_;
        const auto r = modeBtn_[static_cast<size_t>(i)].toFloat();
        g.setColour(sel ? kCyan.withAlpha(0.16f) : kPanel.brighter(0.06f));
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(sel ? kCyan : kBorder);
        g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
        auto box = modeBtn_[static_cast<size_t>(i)];
        auto subRow = box.removeFromBottom(13);
        g.setColour(sel ? kText : kDim);
        g.setFont(FontManager::getInstance().getUIFontBold(11.0f));
        g.drawText(kModes[i].name, box, juce::Justification::centred);
        g.setColour(sel ? kCyan : kDim.withAlpha(0.7f));
        g.setFont(FontManager::getInstance().getUIFont(8.5f));
        g.drawText(kModes[i].sub, subRow, juce::Justification::centred);
    }

    // BLEND ROUTES label (the four value boxes paint themselves as sliders).
    g.setColour(kDim);
    g.setFont(FontManager::getInstance().getUIFontBold(9.0f));
    g.drawText("BLEND ROUTES", blendLabelRect_, juce::Justification::centredLeft);
}

void NimbusUI::mouseDown(const juce::MouseEvent& e) {
    const auto p = e.getPosition();
    if (freezeBtn_.contains(p)) {
        freeze_ = !freeze_;
        if (onParameterChanged)
            onParameterChanged(kFreeze, freeze_ ? 1.0f : 0.0f);
        repaint();
        return;
    }
    for (int i = 0; i < kNumModes; ++i) {
        if (modeBtn_[static_cast<size_t>(i)].contains(p)) {
            curMode_ = i;
            if (onParameterChanged)
                onParameterChanged(kMode, static_cast<float>(i));
            repaint();
            return;
        }
    }
}

void NimbusUI::layoutRow(juce::Rectangle<int> row, const std::vector<int>& indices) {
    const int cellW = row.getWidth() / static_cast<int>(indices.size());
    for (int idx : indices) {
        auto cell = row.removeFromLeft(cellW).reduced(6, 0);
        auto& c = controls_[static_cast<size_t>(idx)];
        c.label->setBounds(cell.removeFromTop(14));
        c.slider->setBounds(cell.removeFromTop(30));
    }
}

void NimbusUI::resized() {
    auto r = getLocalBounds().reduced(8);

    // GRAIN BUFFER spectrum on top (absorbs vertical slack).
    const int bottomH = 230;
    grainArea_ = r.removeFromTop(juce::jmax(180, r.getHeight() - bottomH - 8));
    r.removeFromTop(8);

    {
        auto a = grainArea_.reduced(12, 10);
        a.removeFromTop(22);
        bufferVizArea_ = a;
    }

    // Bottom split: PARAMETERS (left) | controls (right).
    paramsArea_ = r.removeFromLeft(r.getWidth() * 11 / 20 - 4);
    r.removeFromLeft(8);
    ctrlArea_ = r;

    // PARAMETERS: Position/Size/Pitch, Density/Texture, then the blend routes.
    {
        auto a = paramsArea_.reduced(12, 10);
        a.removeFromTop(22);
        const int rowH = 48;
        layoutRow(a.removeFromTop(rowH), {kPosition, kSize, kPitch});
        a.removeFromTop(10);
        layoutRow(a.removeFromTop(rowH), {kDensity, kTexture});
        a.removeFromTop(16);
        blendLabelRect_ = a.removeFromTop(14);
        a.removeFromTop(4);
        layoutRow(a.removeFromTop(rowH), {kDryWet, kSpread, kFeedback, kReverb});
    }

    // Right controls: FREEZE, then PLAYBACK MODE (2x2) filling the rest.
    {
        auto a = ctrlArea_.reduced(12, 12);
        freezeBtn_ = a.removeFromTop(46);
        a.removeFromTop(14);

        modeLabelRect_ = a.removeFromTop(14);
        a.removeFromTop(8);
        auto modeGrid = a;  // fill the remaining panel height
        const int gx = 6, gy = 6;
        const int cw = (modeGrid.getWidth() - gx) / 2;
        const int ch = (modeGrid.getHeight() - gy) / 2;
        for (int i = 0; i < kNumModes; ++i) {
            const int row = i / 2, col = i % 2;
            modeBtn_[static_cast<size_t>(i)] = juce::Rectangle<int>(
                modeGrid.getX() + col * (cw + gx), modeGrid.getY() + row * (ch + gy), cw, ch);
        }
    }
}

}  // namespace magda::daw::ui
