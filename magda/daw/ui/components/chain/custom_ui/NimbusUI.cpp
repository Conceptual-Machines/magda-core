#include "custom_ui/NimbusUI.hpp"

#include <cmath>

#include "audio/plugins/mutable/MutableCloudsPlugin.hpp"
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

    // Colour-group the grain controls (matches Materia's accents): Position and
    // Density in pink, Size and Texture in teal.
    auto colour = [this](int idx, juce::Colour c) {
        controls_[static_cast<size_t>(idx)].slider->setTextColour(c);
        controls_[static_cast<size_t>(idx)].label->setColour(juce::Label::textColourId, c);
    };
    colour(kPosition, kPink);
    colour(kDensity, kPink);
    colour(kSize, kCyan);
    colour(kTexture, kCyan);

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

void NimbusUI::setPlugin(daw::audio::MutableCloudsPlugin* plugin) {
    plugin_ = plugin;
}

void NimbusUI::paintBuffer(juce::Graphics& g) {
    auto b = bufferVizArea_;
    g.setColour(juce::Colour{0xff0a1416});
    g.fillRoundedRectangle(b.toFloat(), 4.0f);
    g.setColour(kBorder);
    g.drawRoundedRectangle(b.toFloat().reduced(0.5f), 4.0f, 1.0f);

    auto plot = b.reduced(14, 14);

    // Recent input level drives the cloud's liveliness (ambient, not a buffer).
    float energy = 0.0f;
    bool haveAudio = false;
    if (plugin_ != nullptr && plugin_->inputEnvelopeTap().writePosition() > 0) {
        float env[64];
        plugin_->inputEnvelopeTap().readLatest(env, 64);
        float s = 0.0f;
        for (float e : env)
            s += e;
        energy = juce::jlimit(0.0f, 1.0f, s / 64.0f * 1.6f);
        haveAudio = true;
    }
    const float life = haveAudio ? (0.22f + 0.78f * energy) : 0.5f;  // gentle idle baseline

    const float position = (float)controls_[kPosition].slider->getValue();
    const float size = (float)controls_[kSize].slider->getValue();
    const float density = (float)controls_[kDensity].slider->getValue();
    const float texture = (float)controls_[kTexture].slider->getValue();

    // Cloud drifts horizontally with Position, widens with Size, spreads and
    // jitters with Texture, thickens with Density.
    const float cloudW =
        juce::jmap(size, 0.0f, 1.0f, plot.getWidth() * 0.25f, (float)plot.getWidth());
    const float cloudCx = plot.getX() + position * plot.getWidth();
    const float cyc = plot.getCentreY();
    const float spreadY = plot.getHeight() * (0.2f + 0.7f * texture);

    g.saveState();
    g.reduceClipRegion(b);
    const int n = 20 + (int)std::lround(density * 130.0f);
    for (int k = 0; k < n; ++k) {
        const float bx = hash1(k * 2 + 1) * 2.0f - 1.0f;
        const float by = hash1(k * 2 + 2) * 2.0f - 1.0f;
        const float jitter = 1.5f + 5.0f * texture;
        const float px =
            cloudCx + bx * cloudW * 0.5f + std::sin(animPhase_ * 1.1f + k * 1.7f) * jitter;
        const float py = cyc + by * spreadY * 0.5f + std::cos(animPhase_ + k * 2.3f) * jitter;
        const float shimmer = 0.3f + 0.7f * std::abs(std::sin(animPhase_ * 0.9f + k));
        const float a = juce::jlimit(0.0f, 1.0f, life * shimmer);
        const float rad = 1.3f + 1.8f * hash1(k * 2 + 5);
        g.setColour(kGrain.withAlpha(a * 0.28f));
        g.fillEllipse(px - rad * 2.0f, py - rad * 2.0f, rad * 4.0f, rad * 4.0f);  // glow
        g.setColour(kGrain.withAlpha(a));
        g.fillEllipse(px - rad, py - rad, rad * 2.0f, rad * 2.0f);
    }
    g.restoreState();

    // Freeze / mode indicator (top-right).
    auto ind = b.reduced(12, 8).removeFromTop(14);
    g.setFont(FontManager::getInstance().getUIFontBold(9.0f));
    g.setColour(kDim);
    g.drawText(juce::String(kModes[curMode_].name), ind, juce::Justification::topRight);
    g.setColour(freeze_ ? kCyan : kDim.withAlpha(0.6f));
    g.drawText(juce::String::fromUTF8("\xe2\x97\x8f ") + "FROZEN", ind.withTrimmedRight(74),
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
    titleStrip(g, grainArea_.reduced(14, 10), "GRAIN CLOUD", "granular texture");
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
        const bool showSub = box.getHeight() >= 30;  // hide subtitle when cramped
        auto subRow = showSub ? box.removeFromBottom(12) : juce::Rectangle<int>();
        g.setColour(sel ? kText : kDim);
        g.setFont(FontManager::getInstance().getUIFontBold(11.0f));
        g.drawText(kModes[i].name, box, juce::Justification::centred);
        if (showSub) {
            g.setColour(sel ? kCyan : kDim.withAlpha(0.7f));
            g.setFont(FontManager::getInstance().getUIFont(8.5f));
            g.drawText(kModes[i].sub, subRow, juce::Justification::centred);
        }
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
        c.label->setBounds(cell.removeFromTop(13));
        c.slider->setBounds(cell.removeFromTop(24));
    }
}

void NimbusUI::resized() {
    auto r = getLocalBounds().reduced(8);

    // Split the height evenly so the top row (cloud + controls) and the
    // PARAMETERS panel stretch together as the slot grows.
    auto top = r.removeFromTop((r.getHeight() - 8) / 2);
    r.removeFromTop(8);
    paramsArea_ = r;

    // Top row: GRAIN CLOUD (left) | FREEZE + PLAYBACK MODE controls (right).
    grainArea_ = top.removeFromLeft(top.getWidth() / 2 - 4);
    top.removeFromLeft(8);
    ctrlArea_ = top;

    {
        auto a = grainArea_.reduced(12, 10);
        a.removeFromTop(22);
        bufferVizArea_ = a;
    }

    // PARAMETERS: Position/Size/Pitch/Density/Texture, then the blend routes.
    {
        auto a = paramsArea_.reduced(12, 8);
        a.removeFromTop(18);  // title strip
        const int rowH = 38;
        // Centre the two control rows in the panel body so extra height reads as
        // balanced margin rather than dead space at the bottom.
        const int blockH = rowH + 8 + 12 + 2 + rowH;
        a.removeFromTop(juce::jmax(0, (a.getHeight() - blockH) / 2));
        layoutRow(a.removeFromTop(rowH), {kPosition, kDensity, kSize, kTexture, kPitch});
        a.removeFromTop(8);
        blendLabelRect_ = a.removeFromTop(12);
        a.removeFromTop(2);
        layoutRow(a.removeFromTop(rowH), {kDryWet, kSpread, kFeedback, kReverb});
    }

    // Top-right controls: FREEZE, then PLAYBACK MODE (2x2) filling the rest.
    {
        auto a = ctrlArea_.reduced(12, 10);
        freezeBtn_ = a.removeFromTop(28);
        a.removeFromTop(10);

        modeLabelRect_ = a.removeFromTop(13);
        a.removeFromTop(6);
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
