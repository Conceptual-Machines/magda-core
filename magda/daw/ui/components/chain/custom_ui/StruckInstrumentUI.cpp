#include "custom_ui/StruckInstrumentUI.hpp"

#include <cmath>

#include "audio/plugins/compiled/MagdaCompiledPolyInstrument.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {
const juce::Colour kBg{0xff0d0d0f};
const juce::Colour kPanel{0xff141417};
const juce::Colour kBorder{0xff242428};
const juce::Colour kText{0xffe4e4e8};
const juce::Colour kDim{0xff7a7a84};
const juce::Colour kExc{0xffd6a24c};   // exciter accent (amber)
const juce::Colour kReso{0xff5b8fd0};  // resonator accent (blue)

constexpr int kTitleH = 18;
constexpr int kRowH = 44;
constexpr int kGap = 8;

struct StruckDevice {
    const char* pluginId;
    const char* title;
};
constexpr StruckDevice kDevices[] = {
    {"magda_marimba", "Marimba"},
    {"magda_djembe", "Djembe"},
    {"magda_bell", "Bell"},
};

// UI label from the host slot name ("strike_position" -> "Position",
// "decay" -> "Decay", "inharmonicity" -> "Inharmonicity").
juce::String displayLabel(const juce::String& hostName) {
    juce::String role = hostName.startsWith("strike_")
                            ? hostName.fromFirstOccurrenceOf("_", false, false)
                            : hostName;
    auto words = juce::StringArray::fromTokens(role.replaceCharacter('_', ' '), " ", "");
    for (auto& w : words)
        if (w.isNotEmpty())
            w = w.substring(0, 1).toUpperCase() + w.substring(1);
    return words.joinIntoString(" ");
}
}  // namespace

bool StruckInstrumentUI::handles(const juce::String& pluginId) {
    for (const auto& d : kDevices)
        if (pluginId.equalsIgnoreCase(d.pluginId))
            return true;
    return false;
}

StruckInstrumentUI::StruckInstrumentUI(const juce::String& pluginId)
    : kind_(pluginId.equalsIgnoreCase("magda_djembe") ? Kind::Djembe
            : pluginId.equalsIgnoreCase("magda_bell") ? Kind::Bell
                                                      : Kind::Marimba) {
    for (const auto& d : kDevices)
        if (pluginId.equalsIgnoreCase(d.pluginId))
            title_ = d.title;

    // Slot membership matches each device's voiceSlotInfos() order, with Gain
    // appended by MagdaCompiledPolyInstrument.
    if (kind_ == Kind::Djembe) {
        exciterSlots_ = {0, 1};       // Position, Sharpness
        resonatorSlots_ = {2, 3, 4};  // Decay, Spacing, Inharmonicity
        gainSlot_ = 5;
    } else {                        // Marimba / Bell
        exciterSlots_ = {0, 1, 2};  // Position, Tone, Sharpness
        resonatorSlots_ = {3};      // Decay
        gainSlot_ = 4;
    }
    ensureControls(gainSlot_ + 1);
}

StruckInstrumentUI::~StruckInstrumentUI() {
    stopTimer();
}

void StruckInstrumentUI::ensureControls(int count) {
    while (static_cast<int>(controls_.size()) < count) {
        const int idx = static_cast<int>(controls_.size());
        Control c;
        c.label = std::make_unique<juce::Label>();
        c.label->setFont(FontManager::getInstance().getUIFont(11.0f));
        c.label->setColour(juce::Label::textColourId, kDim);
        c.label->setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(*c.label);

        c.slider = std::make_unique<LinkableTextSlider>();
        c.slider->setParamIndex(idx);
        c.slider->onValueChanged = [this, idx](double v) {
            if (onParameterChanged)
                onParameterChanged(idx, static_cast<float>(v));
            repaint();  // body strike dot tracks the Position knob
        };
        addAndMakeVisible(*c.slider);
        controls_.push_back(std::move(c));
    }
    slotMin_.resize(controls_.size(), 0.0f);
    slotMax_.resize(controls_.size(), 1.0f);
}

void StruckInstrumentUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    for (const auto& info : params) {
        if (info.paramIndex < 0 || info.paramIndex >= static_cast<int>(controls_.size()))
            continue;
        auto& c = controls_[static_cast<size_t>(info.paramIndex)];
        c.label->setText(displayLabel(info.name), juce::dontSendNotification);
        c.slider->setParameterInfo(info);
        c.slider->setValue(info.currentValue, juce::dontSendNotification);
        slotMin_[static_cast<size_t>(info.paramIndex)] = info.minValue;
        slotMax_[static_cast<size_t>(info.paramIndex)] = info.maxValue;
    }
    resized();
    repaint();
}

std::vector<LinkableTextSlider*> StruckInstrumentUI::getLinkableSliders() {
    std::vector<LinkableTextSlider*> out;
    out.reserve(controls_.size());
    for (auto& c : controls_)
        out.push_back(c.slider.get());
    return out;
}

int StruckInstrumentUI::preferredContentWidth() const {
    return 560;  // body panel + EXCITER | RESONATOR columns
}

void StruckInstrumentUI::setLivePlugin(
    magda::daw::audio::compiled::MagdaCompiledPolyInstrument* plugin) {
    plugin_ = plugin;
    if (plugin_ != nullptr) {
        lastStrikePulse_ = plugin_->strikePulse();
        startTimerHz(30);
    } else {
        stopTimer();
    }
}

void StruckInstrumentUI::timerCallback() {
    if (plugin_ != nullptr) {
        const auto pulse = plugin_->strikePulse();
        if (pulse != lastStrikePulse_) {
            lastStrikePulse_ = pulse;
            flash_ = 1.0f;
        }
    }
    if (flash_ > 0.01f) {
        flash_ *= 0.82f;
        repaint(bodyArea_);
    } else if (flash_ != 0.0f) {
        flash_ = 0.0f;
        repaint(bodyArea_);
    }
}

float StruckInstrumentUI::positionValue() const {
    if (kPositionSlot >= static_cast<int>(controls_.size()))
        return 0.0f;
    return juce::jlimit(0.0f, 1.0f,
                        static_cast<float>(controls_[kPositionSlot].slider->getValue()));
}

float StruckInstrumentUI::decayNorm() const {
    const int s = resonatorSlots_.empty() ? -1 : resonatorSlots_[0];  // Decay is first reso slot
    if (s < 0 || s >= static_cast<int>(controls_.size()))
        return 0.0f;
    const float lo = slotMin_[static_cast<size_t>(s)];
    const float hi = slotMax_[static_cast<size_t>(s)];
    if (hi <= lo)
        return 0.0f;
    return juce::jlimit(
        0.0f, 1.0f,
        (static_cast<float>(controls_[static_cast<size_t>(s)].slider->getValue()) - lo) /
            (hi - lo));
}

// The body graphic occupies the body panel minus padding and a caption strip.
static juce::Rectangle<float> graphicRect(juce::Rectangle<int> bodyArea) {
    return bodyArea.reduced(18).withTrimmedBottom(14).toFloat();
}

juce::Point<float> StruckInstrumentUI::strikePoint() const {
    const auto gr = graphicRect(bodyArea_);
    const float pos = positionValue();
    switch (kind_) {
        case Kind::Marimba: {
            const float y = gr.getY() + gr.getHeight() * 0.40f;  // bar centre
            return {gr.getX() + pos * gr.getWidth(), y};
        }
        case Kind::Djembe: {
            const auto c = gr.getCentre();
            const float maxR = juce::jmin(gr.getWidth(), gr.getHeight()) * 0.45f;
            const float r = pos * maxR;
            return {c.x + std::cos(djembeAngle_) * r, c.y + std::sin(djembeAngle_) * r};
        }
        case Kind::Bell:
        default:
            return {gr.getCentreX(), gr.getBottom() - pos * gr.getHeight()};
    }
}

void StruckInstrumentUI::setPositionFromPoint(juce::Point<int> p) {
    const auto gr = graphicRect(bodyArea_);
    const auto pf = p.toFloat();
    float pos = 0.0f;
    switch (kind_) {
        case Kind::Marimba:
            pos = (pf.x - gr.getX()) / juce::jmax(1.0f, gr.getWidth());
            break;
        case Kind::Djembe: {
            const auto c = gr.getCentre();
            const float maxR = juce::jmin(gr.getWidth(), gr.getHeight()) * 0.45f;
            const auto v = pf - c;
            pos = v.getDistanceFromOrigin() / juce::jmax(1.0f, maxR);
            // Track the drag direction so the dot follows the mouse rather than
            // snapping to a fixed axis; ignore the angle near the centre where it
            // is ill-defined (avoids jitter / sudden flips).
            if (v.getDistanceFromOrigin() > 3.0f)
                djembeAngle_ = std::atan2(v.y, v.x);
            break;
        }
        case Kind::Bell:
            pos = (gr.getBottom() - pf.y) / juce::jmax(1.0f, gr.getHeight());
            break;
    }
    pos = juce::jlimit(0.0f, 1.0f, pos);
    if (kPositionSlot < static_cast<int>(controls_.size())) {
        controls_[kPositionSlot].slider->setValue(pos, juce::dontSendNotification);
        if (onParameterChanged)
            onParameterChanged(kPositionSlot, pos);
        repaint();
    }
}

void StruckInstrumentUI::paintBody(juce::Graphics& g) {
    const auto gr = graphicRect(bodyArea_);
    const float decay = decayNorm();
    const auto strike = strikePoint();

    g.setColour(kText.withAlpha(0.85f));
    juce::Path body;
    switch (kind_) {
        case Kind::Marimba: {
            // Tone bar + resonator tube beneath it.
            const float barH = juce::jmin(30.0f, gr.getHeight() * 0.3f);
            juce::Rectangle<float> bar(gr.getX(), gr.getY() + gr.getHeight() * 0.40f - barH * 0.5f,
                                       gr.getWidth(), barH);
            body.addRoundedRectangle(bar, 5.0f);
            const float tubeW = gr.getWidth() * 0.5f;
            juce::Rectangle<float> tube(gr.getCentreX() - tubeW * 0.5f, bar.getBottom() + 6.0f,
                                        tubeW, gr.getHeight() * 0.32f);
            g.setColour(kPanel.brighter(0.18f));
            g.fillRoundedRectangle(tube, 4.0f);
            g.setColour(kBorder);
            g.drawRoundedRectangle(tube.reduced(0.5f), 4.0f, 1.0f);
            g.setColour(kExc.withAlpha(0.20f));
            g.fillPath(body);
            g.setColour(kText.withAlpha(0.85f));
            g.strokePath(body, juce::PathStrokeType(1.4f));
            break;
        }
        case Kind::Djembe: {
            // Top-down membrane: filled disc with concentric rings.
            const auto c = gr.getCentre();
            const float r = juce::jmin(gr.getWidth(), gr.getHeight()) * 0.45f;
            g.setColour(kExc.withAlpha(0.12f));
            g.fillEllipse(juce::Rectangle<float>(2 * r, 2 * r).withCentre(c));
            g.setColour(kBorder);
            for (int i = 1; i <= 4; ++i) {
                const float rr = r * static_cast<float>(i) / 4.0f;
                g.drawEllipse(juce::Rectangle<float>(2 * rr, 2 * rr).withCentre(c), 1.0f);
            }
            g.setColour(kText.withAlpha(0.85f));
            g.drawEllipse(juce::Rectangle<float>(2 * r, 2 * r).withCentre(c), 1.6f);
            break;
        }
        case Kind::Bell: {
            // Bell cross-section: narrow crown flaring to a wide rim.
            const float cx = gr.getCentreX();
            const float top = gr.getY() + gr.getHeight() * 0.06f;
            const float bot = gr.getBottom();
            const float crownW = gr.getWidth() * 0.16f;
            const float rimW = gr.getWidth() * 0.78f;
            body.startNewSubPath(cx - crownW * 0.5f, top);
            body.cubicTo(cx - crownW * 0.5f, top + (bot - top) * 0.35f, cx - rimW * 0.5f,
                         bot - (bot - top) * 0.30f, cx - rimW * 0.5f, bot);
            body.lineTo(cx + rimW * 0.5f, bot);
            body.cubicTo(cx + rimW * 0.5f, bot - (bot - top) * 0.30f, cx + crownW * 0.5f,
                         top + (bot - top) * 0.35f, cx + crownW * 0.5f, top);
            body.closeSubPath();
            g.setColour(kExc.withAlpha(0.16f));
            g.fillPath(body);
            g.setColour(kText.withAlpha(0.85f));
            g.strokePath(body, juce::PathStrokeType(1.4f));
            // Crown cap.
            g.fillEllipse(
                juce::Rectangle<float>(crownW * 0.8f, crownW * 0.5f).withCentre({cx, top}));
            break;
        }
    }

    // Decay cue: a faint resonance ring around the strike point, growing with
    // Decay; and the strike flash on top.
    const float ringR = 8.0f + decay * 26.0f;
    g.setColour(kReso.withAlpha(0.10f + 0.18f * decay));
    g.drawEllipse(juce::Rectangle<float>(2 * ringR, 2 * ringR).withCentre(strike), 1.4f);

    if (flash_ > 0.0f) {
        const float fr = 10.0f + flash_ * 16.0f;
        g.setColour(kExc.withAlpha(0.55f * flash_));
        g.fillEllipse(juce::Rectangle<float>(2 * fr, 2 * fr).withCentre(strike));
    }

    // Strike dot.
    g.setColour(kBg);
    g.fillEllipse(juce::Rectangle<float>(13, 13).withCentre(strike));
    g.setColour(kExc);
    g.fillEllipse(juce::Rectangle<float>(10, 10).withCentre(strike));
    g.setColour(kText);
    g.drawEllipse(juce::Rectangle<float>(10, 10).withCentre(strike), 1.3f);

    // Caption.
    g.setColour(kDim);
    g.setFont(FontManager::getInstance().getUIFont(9.5f));
    g.drawText("STRIKE POSITION", bodyArea_.reduced(10, 6), juce::Justification::bottomLeft, false);
}

void StruckInstrumentUI::paint(juce::Graphics& g) {
    g.fillAll(kBg);

    auto panel = [&](juce::Rectangle<int> r) {
        g.setColour(kPanel);
        g.fillRoundedRectangle(r.toFloat(), 6.0f);
        g.setColour(kBorder);
        g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 6.0f, 1.0f);
    };

    g.setColour(kText);
    g.setFont(FontManager::getInstance().getUIFontBold(11.5f));
    g.drawText(title_, getLocalBounds().removeFromTop(kTitleH).reduced(8, 0),
               juce::Justification::centredLeft, false);

    panel(bodyArea_);
    panel(exciterArea_);
    panel(resonatorArea_);

    auto sectionTitle = [&](juce::Rectangle<int> a, const juce::String& s, juce::Colour c) {
        g.setColour(c);
        g.setFont(FontManager::getInstance().getUIFontBold(9.5f));
        g.drawText(s, a.reduced(12, 8).removeFromTop(13), juce::Justification::topLeft, false);
    };
    sectionTitle(exciterArea_, "EXCITER", kExc);
    sectionTitle(resonatorArea_, "RESONATOR", kReso);

    paintBody(g);
}

void StruckInstrumentUI::layoutColumn(juce::Rectangle<int> area, const std::vector<int>& slots,
                                      int rowH, int gap) {
    const int rows = static_cast<int>(slots.size());
    if (rows == 0)
        return;
    const int stackH = rows * rowH + (rows - 1) * gap;
    const int top = area.getY() + juce::jmax(0, (area.getHeight() - stackH) / 2);
    juce::Rectangle<int> stack(area.getX(), top, area.getWidth(), stackH);
    for (int slot : slots) {
        if (slot < 0 || slot >= static_cast<int>(controls_.size()))
            continue;
        auto row = stack.removeFromTop(rowH);
        auto& c = controls_[static_cast<size_t>(slot)];
        c.label->setBounds(row.removeFromTop(14));
        c.slider->setBounds(row.reduced(0, 1));
        stack.removeFromTop(gap);
    }
}

void StruckInstrumentUI::resized() {
    auto r = getLocalBounds().reduced(8);
    r.removeFromTop(kTitleH);  // title strip (painted)

    bodyArea_ = r.removeFromLeft(juce::jmax(180, r.getWidth() * 42 / 100));
    r.removeFromLeft(8);
    exciterArea_ = r.removeFromLeft(r.getWidth() / 2 - 4);
    r.removeFromLeft(8);
    resonatorArea_ = r;

    // EXCITER column.
    {
        auto a = exciterArea_.reduced(12, 10);
        a.removeFromTop(20);  // section title
        layoutColumn(a, exciterSlots_, kRowH, kGap);
    }
    // RESONATOR column (+ Gain pinned underneath the resonator knobs).
    {
        auto a = resonatorArea_.reduced(12, 10);
        a.removeFromTop(20);
        std::vector<int> slots = resonatorSlots_;
        if (gainSlot_ >= 0)
            slots.push_back(gainSlot_);
        layoutColumn(a, slots, kRowH, kGap);
    }
}

void StruckInstrumentUI::mouseDown(const juce::MouseEvent& e) {
    if (bodyArea_.contains(e.getPosition()))
        setPositionFromPoint(e.getPosition());
}

void StruckInstrumentUI::mouseDrag(const juce::MouseEvent& e) {
    if (bodyArea_.contains(e.getMouseDownPosition()))
        setPositionFromPoint(e.getPosition());
}

}  // namespace magda::daw::ui
