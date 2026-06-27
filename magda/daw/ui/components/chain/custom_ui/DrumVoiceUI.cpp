#include "custom_ui/DrumVoiceUI.hpp"

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {
constexpr int kSectionTitleH = 15;
constexpr int kEnvH = 26;  // per-section envelope graph strip
constexpr int kCellLabelH = 12;
constexpr int kCellPad = 4;
constexpr int kCellW = 76;  // per-knob column width (preferred sizing)

struct DrumVoice {
    const char* pluginId;
    const char* title;
};
constexpr DrumVoice kDrumVoices[] = {
    {"magda_kick", "Kick"}, {"magda_snare", "Snare"}, {"magda_clap", "Clap"},
    {"magda_hat", "Hat"},   {"magda_tom", "Tom"},
};
}  // namespace

bool DrumVoiceUI::handles(const juce::String& pluginId) {
    return !titleFor(pluginId).isEmpty();
}

juce::String DrumVoiceUI::titleFor(const juce::String& pluginId) {
    for (const auto& v : kDrumVoices)
        if (pluginId.equalsIgnoreCase(v.pluginId))
            return v.title;
    return {};
}

std::vector<DrumVoiceUI::Section> DrumVoiceUI::sectionsFor(const juce::String& pluginId) {
    // Slot indices match the [idx:N] pins in each voice's .dsp. decaySlot (and
    // attackSlot when the layer has one) drive the per-section envelope graph.
    if (pluginId.equalsIgnoreCase("magda_tom"))
        return {
            {.title = "Body", .slots = {0, 1, 2, 3}, .attackSlot = 2, .decaySlot = 3},
            {.title = "Noise", .slots = {4, 5, 6}, .decaySlot = 6},
        };
    if (pluginId.equalsIgnoreCase("magda_hat"))
        return {
            {.title = "Ring", .slots = {0, 1, 2, 3}, .decaySlot = 3},
            {.title = "Noise", .slots = {4, 5, 6}, .decaySlot = 6},
        };
    if (pluginId.equalsIgnoreCase("magda_kick"))
        return {
            {.title = "Transient", .slots = {0, 1, 2, 3}, .decaySlot = 3},
            {.title = "Body", .slots = {4, 5, 6, 7, 8, 9}, .attackSlot = 7, .decaySlot = 8},
            {.title = "Click", .slots = {10, 11}},
        };
    if (pluginId.equalsIgnoreCase("magda_snare"))
        return {
            {.title = "Transient", .slots = {0, 1, 2, 3, 4}, .decaySlot = 3},
            {.title = "Body", .slots = {5, 6, 7, 8, 9}, .attackSlot = 8, .decaySlot = 9},
            {.title = "Rattle", .slots = {10, 11, 12, 13, 14, 15}, .decaySlot = 14},
        };
    if (pluginId.equalsIgnoreCase("magda_clap"))
        return {
            {.title = "Flam", .slots = {0, 1, 2, 3}, .decaySlot = 2},
            {.title = "Shape", .slots = {4, 5, 6}},
        };
    return {};  // other voices: single flat row
}

DrumVoiceUI::DrumVoiceUI(const juce::String& pluginId)
    : title_(titleFor(pluginId)), sections_(sectionsFor(pluginId)) {}
DrumVoiceUI::~DrumVoiceUI() = default;

void DrumVoiceUI::ensureControls(int count) {
    while (static_cast<int>(controls_.size()) < count) {
        const int idx = static_cast<int>(controls_.size());
        Control c;

        c.label = std::make_unique<juce::Label>();
        c.label->setFont(FontManager::getInstance().getUIFont(10.0f));
        c.label->setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
        c.label->setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(*c.label);

        c.slider = std::make_unique<LinkableTextSlider>();
        c.slider->setParamIndex(idx);
        c.slider->onValueChanged = [this, idx](double v) {
            if (onParameterChanged)
                onParameterChanged(idx, static_cast<float>(v));
            repaint();  // refresh the envelope graphs
        };
        addAndMakeVisible(*c.slider);

        controls_.push_back(std::move(c));
    }
}

void DrumVoiceUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    ensureControls(static_cast<int>(params.size()));

    for (const auto& info : params) {
        if (info.paramIndex < 0 || info.paramIndex >= static_cast<int>(controls_.size()))
            continue;
        auto& c = controls_[static_cast<size_t>(info.paramIndex)];
        c.label->setText(info.name, juce::dontSendNotification);
        c.slider->setParameterInfo(info);
        c.slider->setValue(info.currentValue, juce::dontSendNotification);
    }
    resized();
    repaint();  // envelope graphs reflect the new values
}

std::vector<LinkableTextSlider*> DrumVoiceUI::getLinkableSliders() {
    std::vector<LinkableTextSlider*> sliders;
    sliders.reserve(controls_.size());
    for (auto& c : controls_)
        sliders.push_back(c.slider.get());
    return sliders;
}

int DrumVoiceUI::preferredContentWidth() const {
    int knobs = static_cast<int>(controls_.size());
    if (!sections_.empty()) {
        knobs = 0;
        for (const auto& s : sections_)
            knobs += static_cast<int>(s.slots.size());
    }
    return juce::jmax(1, knobs) * kCellW + 2 * kCellPad;
}

void DrumVoiceUI::layoutRow(juce::Rectangle<int> area, const std::vector<int>& slots) {
    if (slots.empty())
        return;
    const int cellW = area.getWidth() / static_cast<int>(slots.size());
    for (int idx : slots) {
        if (idx < 0 || idx >= static_cast<int>(controls_.size()))
            continue;
        auto& c = controls_[static_cast<size_t>(idx)];
        auto cell = area.removeFromLeft(cellW).reduced(2, 0);
        c.label->setBounds(cell.removeFromTop(kCellLabelH));
        c.slider->setBounds(cell.removeFromTop(juce::jmin(22, cell.getHeight())));
    }
}

void DrumVoiceUI::paint(juce::Graphics& g) {
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(getLocalBounds());

    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.setFont(FontManager::getInstance().getUIFont(11.0f));

    auto titleArea = getLocalBounds().removeFromTop(kSectionTitleH).reduced(kCellPad, 0);
    g.drawText(title_, titleArea, juce::Justification::centredLeft, false);

    // Time axis shared across sections so layer lengths compare visually
    // (attack + decay, in ms).
    float axisMaxMs = 1.0f;
    for (const auto& s : sections_) {
        if (s.decaySlot < 0 || s.decaySlot >= static_cast<int>(controls_.size()))
            continue;
        float t =
            static_cast<float>(controls_[static_cast<size_t>(s.decaySlot)].slider->getValue());
        if (s.attackSlot >= 0 && s.attackSlot < static_cast<int>(controls_.size()))
            t +=
                static_cast<float>(controls_[static_cast<size_t>(s.attackSlot)].slider->getValue());
        axisMaxMs = juce::jmax(axisMaxMs, t);
    }

    // Section titles + a divider down the left edge of each section after the
    // first, plus the per-section envelope graph. sectionTitleAreas_ /
    // sectionEnvAreas_ are filled by resized(), 1:1 with sections_.
    for (size_t i = 0; i < sectionTitleAreas_.size() && i < sections_.size(); ++i) {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.setFont(FontManager::getInstance().getUIFont(11.0f));
        g.drawText(sections_[i].title, sectionTitleAreas_[i].reduced(2, 0),
                   juce::Justification::centredLeft, false);
        if (i > 0) {
            const int x = sectionTitleAreas_[i].getX() - kCellPad / 2;
            g.drawVerticalLine(x, static_cast<float>(sectionTitleAreas_[i].getY()),
                               static_cast<float>(getHeight() - kCellPad));
        }
        if (i < sectionEnvAreas_.size())
            drawEnvelope(g, sectionEnvAreas_[i], sections_[i], axisMaxMs);
    }
}

void DrumVoiceUI::resized() {
    sectionTitleAreas_.clear();
    sectionEnvAreas_.clear();
    auto area = getLocalBounds().reduced(kCellPad);
    area.removeFromTop(kSectionTitleH);  // voice-name strip (painted)
    if (controls_.empty())
        return;

    if (sections_.empty()) {
        std::vector<int> all(controls_.size());
        for (int i = 0; i < static_cast<int>(controls_.size()); ++i)
            all[static_cast<size_t>(i)] = i;
        layoutRow(area, all);
        return;
    }

    int totalKnobs = 0;
    for (const auto& s : sections_)
        totalKnobs += static_cast<int>(s.slots.size());
    if (totalKnobs == 0)
        return;

    int x = area.getX();
    for (size_t i = 0; i < sections_.size(); ++i) {
        const auto& s = sections_[i];
        const bool last = (i + 1 == sections_.size());
        const int w = last ? (area.getRight() - x)
                           : juce::roundToInt(area.getWidth() *
                                              (static_cast<double>(s.slots.size()) / totalKnobs));
        juce::Rectangle<int> sa(x, area.getY(), w, area.getHeight());
        sectionTitleAreas_.push_back(sa.removeFromTop(kSectionTitleH));
        sectionEnvAreas_.push_back(sa.removeFromTop(kEnvH).reduced(kCellPad, 2));
        layoutRow(sa.reduced(kCellPad, 0), s.slots);
        x += w;
    }
}

void DrumVoiceUI::drawEnvelope(juce::Graphics& g, juce::Rectangle<int> area, const Section& s,
                               float axisMaxMs) {
    if (s.decaySlot < 0 || s.decaySlot >= static_cast<int>(controls_.size()) || axisMaxMs <= 0.0f)
        return;

    const float attackMs =
        (s.attackSlot >= 0 && s.attackSlot < static_cast<int>(controls_.size()))
            ? static_cast<float>(controls_[static_cast<size_t>(s.attackSlot)].slider->getValue())
            : 0.0f;
    const float decayMs =
        static_cast<float>(controls_[static_cast<size_t>(s.decaySlot)].slider->getValue());

    const auto r = area.toFloat();
    const float xA = r.getX() + r.getWidth() * (attackMs / axisMaxMs);
    const float xD = r.getX() + r.getWidth() * ((attackMs + decayMs) / axisMaxMs);
    const float top = r.getY() + 1.0f;
    const float bot = r.getBottom() - 1.0f;

    // Linear attack ramp to the peak, then linear decay to zero (matches the
    // dsp's linear en.ar / en.adsr segments).
    juce::Path p;
    p.startNewSubPath(r.getX(), bot);
    p.lineTo(xA, top);
    p.lineTo(juce::jmin(xD, r.getRight()), bot);

    auto accent = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);
    g.setColour(accent.withAlpha(0.18f));
    juce::Path fill = p;
    fill.lineTo(r.getX(), bot);
    fill.closeSubPath();
    g.fillPath(fill);
    g.setColour(accent);
    g.strokePath(p, juce::PathStrokeType(1.2f));
}

}  // namespace magda::daw::ui
