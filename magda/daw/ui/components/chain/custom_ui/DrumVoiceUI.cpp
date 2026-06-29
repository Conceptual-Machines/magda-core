#include "custom_ui/DrumVoiceUI.hpp"

#include <cmath>

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {
constexpr int kSectionTitleH = 15;
constexpr int kEnvH = 48;  // per-section envelope graph strip
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

// UI label derived from the composed host name `<section>_<role>` (e.g.
// "transient_level" -> "Level", "body_snap_time" -> "Snap Time"). Sections are
// titled, so the section prefix is dropped and the role is prettified.
juce::String displayLabel(const juce::String& hostName) {
    juce::String role = hostName.fromFirstOccurrenceOf("_", false, false);
    if (role.isEmpty())
        role = hostName;
    auto words = juce::StringArray::fromTokens(role.replaceCharacter('_', ' '), " ", "");
    for (auto& w : words) {
        if (w.equalsIgnoreCase("hp"))
            w = "HP";
        else if (w.isNotEmpty())
            w = w.substring(0, 1).toUpperCase() + w.substring(1);
    }
    return words.joinIntoString(" ");
}
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
            {.title = "Body",
             .slots = {0, 1, 2, 3, 7},
             .attackSlot = 2,
             .decaySlot = 3,
             .curveSlot = 7},
            {.title = "Noise", .slots = {4, 5, 6, 8}, .decaySlot = 6, .curveSlot = 8},
        };
    if (pluginId.equalsIgnoreCase("magda_hat"))
        return {
            {.title = "Ring", .slots = {0, 1, 2, 3, 7}, .decaySlot = 3, .curveSlot = 7},
            {.title = "Noise", .slots = {4, 5, 9, 10, 6, 8}, .decaySlot = 6, .curveSlot = 8},
        };
    if (pluginId.equalsIgnoreCase("magda_kick"))
        return {
            {.title = "Transient", .slots = {0, 1, 2, 3, 13}, .decaySlot = 3, .curveSlot = 13},
            {.title = "Body",
             .slots = {4, 5, 6, 7, 8, 9, 12},
             .attackSlot = 7,
             .decaySlot = 8,
             .curveSlot = 12,
             .cols = 3},
            {.title = "Click", .slots = {10, 11}},
        };
    if (pluginId.equalsIgnoreCase("magda_snare"))
        return {
            {.title = "Transient", .slots = {0, 1, 2, 3, 4, 17}, .decaySlot = 3, .curveSlot = 17},
            {.title = "Body",
             .slots = {5, 6, 7, 8, 9, 16},
             .attackSlot = 8,
             .decaySlot = 9,
             .curveSlot = 16,
             .cols = 2},
            {.title = "Rattle",
             .slots = {10, 11, 12, 13, 14, 15, 18},
             .decaySlot = 14,
             .curveSlot = 18},
        };
    if (pluginId.equalsIgnoreCase("magda_clap"))
        return {
            {.title = "Flam", .slots = {0, 1, 2, 3, 7}, .decaySlot = 2, .curveSlot = 7},
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
    slotMin_.resize(controls_.size(), 0.0f);
    slotMax_.resize(controls_.size(), 1.0f);

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
    if (sections_.empty())
        return juce::jmax(1, static_cast<int>(controls_.size())) * kCellW + 2 * kCellPad;
    int cols = 0;
    for (const auto& s : sections_)
        cols += s.cols;  // each section is `cols` columns wide
    return juce::jmax(1, cols) * kCellW + 2 * kCellPad;
}

void DrumVoiceUI::layoutGrid(juce::Rectangle<int> area, const std::vector<int>& slots, int cols,
                             int rowH) {
    if (slots.empty())
        return;
    cols = juce::jmax(1, cols);
    const int cellW = area.getWidth() / cols;
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        const int idx = slots[static_cast<size_t>(i)];
        if (idx < 0 || idx >= static_cast<int>(controls_.size()))
            continue;
        const int col = i % cols;
        const int row = i / cols;
        auto cell =
            juce::Rectangle<int>(area.getX() + col * cellW, area.getY() + row * rowH, cellW, rowH)
                .reduced(2, 1);
        auto& c = controls_[static_cast<size_t>(idx)];
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

    // Section titles + a divider down the left edge of each section after the
    // first, plus the per-section envelope graph. sectionTitleAreas_ /
    // sectionEnvAreas_ are filled by resized(), 1:1 with sections_. Each
    // envelope is scaled to its OWN decay range (slot max), so changing one
    // section's decay never rescales another's graph.
    for (size_t i = 0; i < sectionTitleAreas_.size() && i < sections_.size(); ++i) {
        const auto& s = sections_[i];
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.setFont(FontManager::getInstance().getUIFont(11.0f));
        g.drawText(s.title, sectionTitleAreas_[i].reduced(2, 0), juce::Justification::centredLeft,
                   false);
        if (i > 0) {
            const int x = sectionTitleAreas_[i].getX() - kCellPad / 2;
            g.drawVerticalLine(x, static_cast<float>(sectionTitleAreas_[i].getY()),
                               static_cast<float>(getHeight() - kCellPad));
        }

        float axisMaxMs = 1.0f;
        if (s.decaySlot >= 0 && s.decaySlot < static_cast<int>(slotMax_.size())) {
            axisMaxMs = slotMax_[static_cast<size_t>(s.decaySlot)];
            if (s.attackSlot >= 0 && s.attackSlot < static_cast<int>(slotMax_.size()))
                axisMaxMs += slotMax_[static_cast<size_t>(s.attackSlot)];
        }
        if (i < sectionEnvAreas_.size())
            drawEnvelope(g, sectionEnvAreas_[i], s, axisMaxMs);
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
        layoutGrid(area, all, static_cast<int>(all.size()), juce::jmin(36, area.getHeight()));
        return;
    }

    int totalCols = 0;
    int maxRows = 1;
    for (const auto& s : sections_) {
        totalCols += s.cols;
        maxRows = juce::jmax(maxRows, (static_cast<int>(s.slots.size()) + s.cols - 1) / s.cols);
    }
    if (totalCols == 0)
        return;

    // One uniform row height across all sections so the grids align.
    const int knobAreaH = juce::jmax(1, area.getHeight() - kSectionTitleH - kEnvH);
    const int rowH = juce::jmax(20, knobAreaH / maxRows);

    int x = area.getX();
    for (size_t i = 0; i < sections_.size(); ++i) {
        const auto& s = sections_[i];
        const bool last = (i + 1 == sections_.size());
        const int w =
            last ? (area.getRight() - x)
                 : juce::roundToInt(area.getWidth() * (static_cast<double>(s.cols) / totalCols));
        juce::Rectangle<int> sa(x, area.getY(), w, area.getHeight());
        sectionTitleAreas_.push_back(sa.removeFromTop(kSectionTitleH));
        sectionEnvAreas_.push_back(sa.removeFromTop(kEnvH).reduced(kCellPad, 2));
        layoutGrid(sa.reduced(kCellPad, 0), s.slots, s.cols, rowH);
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

    // Curve knob (-50..50) -> decay exponent 8^(-c/50), matching the dsp. 0 (or no
    // curve slot) = linear; >0 = swelled/sustained, <0 = fast.
    const float curve =
        (s.curveSlot >= 0 && s.curveSlot < static_cast<int>(controls_.size()))
            ? static_cast<float>(controls_[static_cast<size_t>(s.curveSlot)].slider->getValue())
            : 0.0f;
    const float exponent = std::pow(8.0f, -curve / 50.0f);

    const auto r = area.toFloat();
    const float xA = r.getX() + r.getWidth() * (attackMs / axisMaxMs);
    const float xD =
        juce::jmin(r.getRight(), r.getX() + r.getWidth() * ((attackMs + decayMs) / axisMaxMs));
    const float top = r.getY() + 1.0f;
    const float bot = r.getBottom() - 1.0f;

    // Linear attack ramp to the peak, then the decay as a sampled power curve
    // (env = (1-p)^exponent), matching the dsp's pow(env, ...) shaping.
    juce::Path p;
    p.startNewSubPath(r.getX(), bot);
    p.lineTo(xA, top);
    constexpr int kSteps = 24;
    for (int i = 1; i <= kSteps; ++i) {
        const float pn = static_cast<float>(i) / kSteps;
        const float e = std::pow(1.0f - pn, exponent);
        p.lineTo(xA + (xD - xA) * pn, bot - (bot - top) * e);
    }

    auto accent = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);
    g.setColour(accent.withAlpha(0.18f));
    juce::Path fill = p;
    fill.lineTo(r.getX(), bot);
    fill.closeSubPath();
    g.fillPath(fill);
    g.setColour(accent);
    g.strokePath(p, juce::PathStrokeType(1.2f));

    // Draggable handles: peak (attack, when present) and end (decay).
    constexpr float kDot = 3.0f;
    if (s.attackSlot >= 0)
        g.fillEllipse(xA - kDot, top - kDot, 2 * kDot, 2 * kDot);
    g.fillEllipse(xD - kDot, bot - kDot, 2 * kDot, 2 * kDot);
}

float DrumVoiceUI::sectionAxisMaxMs(const Section& s) const {
    if (s.decaySlot < 0 || s.decaySlot >= static_cast<int>(slotMax_.size()))
        return 1.0f;
    float m = slotMax_[static_cast<size_t>(s.decaySlot)];
    if (s.attackSlot >= 0 && s.attackSlot < static_cast<int>(slotMax_.size()))
        m += slotMax_[static_cast<size_t>(s.attackSlot)];
    return juce::jmax(1.0f, m);
}

bool DrumVoiceUI::envHandles(int i, juce::Point<float>& peak, juce::Point<float>& end) const {
    if (i < 0 || i >= static_cast<int>(sectionEnvAreas_.size()) ||
        i >= static_cast<int>(sections_.size()))
        return false;
    const auto& s = sections_[static_cast<size_t>(i)];
    if (s.decaySlot < 0 || s.decaySlot >= static_cast<int>(controls_.size()))
        return false;
    const auto r = sectionEnvAreas_[static_cast<size_t>(i)].toFloat();
    const float axis = sectionAxisMaxMs(s);
    const float aMs =
        (s.attackSlot >= 0 && s.attackSlot < static_cast<int>(controls_.size()))
            ? static_cast<float>(controls_[static_cast<size_t>(s.attackSlot)].slider->getValue())
            : 0.0f;
    const float dMs =
        static_cast<float>(controls_[static_cast<size_t>(s.decaySlot)].slider->getValue());
    const float xA = r.getX() + r.getWidth() * (aMs / axis);
    const float xD = juce::jmin(r.getRight(), r.getX() + r.getWidth() * ((aMs + dMs) / axis));
    peak = {xA, r.getY() + 1.0f};
    end = {xD, r.getBottom() - 1.0f};
    return true;
}

void DrumVoiceUI::setSlotValue(int slot, float value) {
    if (slot < 0 || slot >= static_cast<int>(controls_.size()))
        return;
    controls_[static_cast<size_t>(slot)].slider->setValue(value, juce::dontSendNotification);
    if (onParameterChanged)
        onParameterChanged(slot, value);
    repaint();
}

void DrumVoiceUI::mouseDown(const juce::MouseEvent& e) {
    dragSection_ = -1;
    dragKind_ = Drag::None;
    const auto pos = e.position;
    for (int i = 0; i < static_cast<int>(sections_.size()); ++i) {
        juce::Point<float> peak, end;
        if (!envHandles(i, peak, end))
            continue;
        const auto& s = sections_[static_cast<size_t>(i)];
        if (s.attackSlot >= 0 && pos.getDistanceFrom(peak) <= 9.0f) {
            dragSection_ = i;
            dragKind_ = Drag::Attack;
            return;
        }
        if (pos.getDistanceFrom(end) <= 9.0f) {
            dragSection_ = i;
            dragKind_ = Drag::Decay;
            return;
        }
    }
}

void DrumVoiceUI::mouseDrag(const juce::MouseEvent& e) {
    if (dragSection_ < 0 || dragKind_ == Drag::None)
        return;
    const auto& s = sections_[static_cast<size_t>(dragSection_)];
    const auto r = sectionEnvAreas_[static_cast<size_t>(dragSection_)].toFloat();
    const float axis = sectionAxisMaxMs(s);
    const float t = juce::jlimit(0.0f, axis, (e.position.x - r.getX()) / r.getWidth() * axis);

    if (dragKind_ == Drag::Attack) {
        setSlotValue(s.attackSlot, juce::jlimit(slotMin_[static_cast<size_t>(s.attackSlot)],
                                                slotMax_[static_cast<size_t>(s.attackSlot)], t));
    } else {  // Decay
        const float aMs = (s.attackSlot >= 0 && s.attackSlot < static_cast<int>(controls_.size()))
                              ? static_cast<float>(
                                    controls_[static_cast<size_t>(s.attackSlot)].slider->getValue())
                              : 0.0f;
        setSlotValue(s.decaySlot,
                     juce::jlimit(slotMin_[static_cast<size_t>(s.decaySlot)],
                                  slotMax_[static_cast<size_t>(s.decaySlot)], t - aMs));
    }
}

void DrumVoiceUI::mouseUp(const juce::MouseEvent&) {
    dragSection_ = -1;
    dragKind_ = Drag::None;
}

void DrumVoiceUI::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    // Scroll over a section's envelope graph to set its Curve.
    for (int i = 0;
         i < static_cast<int>(sectionEnvAreas_.size()) && i < static_cast<int>(sections_.size());
         ++i) {
        const auto& s = sections_[static_cast<size_t>(i)];
        if (s.curveSlot < 0 || s.curveSlot >= static_cast<int>(controls_.size()))
            continue;
        if (!sectionEnvAreas_[static_cast<size_t>(i)].toFloat().contains(e.position))
            continue;
        const float cur =
            static_cast<float>(controls_[static_cast<size_t>(s.curveSlot)].slider->getValue());
        // Scroll down bends the curve down (toward fast/negative); up = swelled/positive.
        const float delta = (wheel.isReversed ? -wheel.deltaY : wheel.deltaY) * 60.0f;
        setSlotValue(s.curveSlot,
                     juce::jlimit(slotMin_[static_cast<size_t>(s.curveSlot)],
                                  slotMax_[static_cast<size_t>(s.curveSlot)], cur + delta));
        return;
    }
}

}  // namespace magda::daw::ui
