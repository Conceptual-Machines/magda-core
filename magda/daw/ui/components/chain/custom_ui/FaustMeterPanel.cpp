#include "custom_ui/FaustMeterPanel.hpp"

#include <cmath>

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

// Same proportions as ParamSlotComponent's name row, so a meter's label sits
// at the height the eye expects from the grid above it.
int labelHeightFor(int cellHeight) {
    return juce::jmin(12, cellHeight / 3);
}

constexpr int kPanelPadding = 4;

int decimalsFor(const magda::MeterInfo& info) {
    // A range of a few units needs a decimal to move at all; a wide one (dB
    // spans, Hz) reads better as a whole number.
    return std::abs(info.maxValue - info.minValue) <= 4.0f ? 2 : 1;
}

}  // namespace

MeterWidget::MeterWidget() {
    setInterceptsMouseClicks(false, false);

    // Seeded from the theme rather than left on JUCE's default, so a strip
    // whose host never calls setFonts still matches the parameter grid.
    nameLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    valueFont_ = FontManager::getInstance().getUIFont(11.0f);
    nameLabel_.setJustificationType(juce::Justification::centredLeft);
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    nameLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(nameLabel_);
}

MeterWidget::~MeterWidget() {
    stopTimer();
}

void MeterWidget::setMeterInfo(const magda::MeterInfo& info) {
    info_ = info;
    nameLabel_.setText(info.name, juce::dontSendNotification);
    // The tooltip goes on this component rather than a child because nothing
    // here intercepts the mouse, so this is what the pointer resolves to.
    setTooltip(info.tooltip);
    value_ = info.minValue;
    repaint();
}

void MeterWidget::setSource(std::function<float()> source) {
    source_ = std::move(source);
    if (source_)
        value_ = source_();
    updateActiveState();
    repaint();
}

void MeterWidget::resized() {
    nameLabel_.setBounds(getLocalBounds().removeFromTop(labelHeightFor(getHeight())));
}

void MeterWidget::visibilityChanged() {
    updateActiveState();
}

void MeterWidget::parentHierarchyChanged() {
    updateActiveState();
}

void MeterWidget::updateActiveState() {
    const bool wanted = source_ != nullptr && isShowing();
    if (wanted == isTimerRunning())
        return;
    if (wanted)
        startTimerHz(kPollHz);
    else
        stopTimer();
}

void MeterWidget::timerCallback() {
    if (!source_)
        return;
    const float value = source_();
    // Repaint on a visible change only: a meter sitting at its floor should
    // cost nothing, and the grid can hold a dozen of these.
    const float span = std::abs(info_.maxValue - info_.minValue);
    const float epsilon = juce::jmax(1.0e-4f, span * 0.001f);
    if (std::abs(value - value_) < epsilon)
        return;
    value_ = value;
    repaint();
}

float MeterWidget::normalized() const {
    const float span = info_.maxValue - info_.minValue;
    if (std::abs(span) < 1.0e-9f)
        return 0.0f;
    return juce::jlimit(0.0f, 1.0f, (value_ - info_.minValue) / span);
}

juce::String MeterWidget::formattedValue() const {
    auto text = juce::String(value_, decimalsFor(info_));
    if (info_.unit.isNotEmpty())
        text += " " + info_.unit;
    return text;
}

void MeterWidget::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    bounds.removeFromTop(labelHeightFor(getHeight()));
    auto readout = bounds.reduced(2);
    if (readout.isEmpty())
        return;

    g.setFont(valueFont_);

    if (info_.style == magda::MeterStyle::Numerical) {
        g.setColour(DarkTheme::getTextColour());
        g.drawText(formattedValue(), readout, juce::Justification::centred, false);
        return;
    }

    if (info_.style == magda::MeterStyle::Led) {
        const float diameter =
            static_cast<float>(juce::jmin(readout.getWidth(), readout.getHeight())) - 2.0f;
        if (diameter <= 0.0f)
            return;
        const auto lamp =
            juce::Rectangle<float>(diameter, diameter).withCentre(readout.toFloat().getCentre());
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.6f));
        g.drawEllipse(lamp, 1.0f);
        // Brightness tracks the value rather than switching at a threshold, so
        // a bargraph reporting a continuous quantity still reads as one.
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_POSITIVE)
                        .withAlpha(juce::jmax(0.08f, normalized())));
        g.fillEllipse(lamp.reduced(1.5f));
        return;
    }

    // A range spanning zero fills from where zero sits rather than from the
    // track's start, so a bipolar readout (boost/cut, ± dB) rests empty at
    // unity and grows the way it is heard.
    const bool bipolar = info_.minValue < 0.0f && info_.maxValue > 0.0f;
    const float origin = bipolar ? -info_.minValue / (info_.maxValue - info_.minValue) : 0.0f;
    const float here = normalized();
    const float from = juce::jmin(origin, here);
    const float to = juce::jmax(origin, here);

    auto track = readout;
    if (info_.vertical) {
        // The figure sits under a vertical bar rather than over it: a column
        // narrow enough to read as a level is too narrow to hold the text.
        auto textRow = track.removeFromBottom(juce::jmin(kVerticalValueHeight, track.getHeight()));
        g.setColour(DarkTheme::getTextColour());
        g.drawText(formattedValue(), textRow, juce::Justification::centred, false);
        track = track.withSizeKeepingCentre(juce::jmin(kVerticalBarWidth, track.getWidth()),
                                            juce::jmax(0, track.getHeight() - 2));
    }
    if (track.isEmpty())
        return;

    const auto trackF = track.toFloat();
    g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
    g.fillRoundedRectangle(trackF, 2.0f);

    // Vertical fills bottom-up, which is the only direction a level reads in.
    const auto fill =
        info_.vertical
            ? juce::Rectangle<float>(trackF.getX(), trackF.getBottom() - trackF.getHeight() * to,
                                     trackF.getWidth(), trackF.getHeight() * (to - from))
            : juce::Rectangle<float>(trackF.getX() + trackF.getWidth() * from, trackF.getY(),
                                     trackF.getWidth() * (to - from), trackF.getHeight());
    if (juce::jmax(fill.getWidth(), fill.getHeight()) > 0.5f) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_POSITIVE).withAlpha(0.55f));
        g.fillRoundedRectangle(fill, 2.0f);
    }

    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.5f));
    g.drawRoundedRectangle(trackF, 2.0f, 1.0f);

    if (!info_.vertical) {
        g.setColour(DarkTheme::getTextColour());
        g.drawText(formattedValue(), readout.reduced(3, 0), juce::Justification::centredRight,
                   false);
    }
}

// ============================================================================
// FaustMeterPanel
// ============================================================================

FaustMeterPanel::FaustMeterPanel() = default;
FaustMeterPanel::~FaustMeterPanel() = default;

bool FaustMeterPanel::setMeters(const std::vector<magda::MeterInfo>& meters) {
    // Rebuilt rather than diffed: the list only changes when a recompile
    // changes what the patch declares, which is rare and already expensive.
    entries_.clear();
    entries_.reserve(meters.size());
    for (const auto& info : meters) {
        Entry entry;
        entry.info = info;
        entry.widget = std::make_unique<MeterWidget>();
        entry.widget->setMeterInfo(info);
        addAndMakeVisible(*entry.widget);
        entries_.push_back(std::move(entry));
    }
    bindSources();
    resized();
    return !entries_.empty();
}

void FaustMeterPanel::setMeterSource(std::function<float(int meterIndex)> source) {
    meterSource_ = std::move(source);
    bindSources();
}

void FaustMeterPanel::bindSources() {
    for (auto& entry : entries_) {
        if (!meterSource_) {
            entry.widget->setSource(nullptr);
            continue;
        }
        const auto source = meterSource_;
        const int meterIndex = entry.info.meterIndex;
        entry.widget->setSource([source, meterIndex]() { return source(meterIndex); });
    }
}

void FaustMeterPanel::paint(juce::Graphics& g) {
    // Rule between the controls and the readouts, matching the one FaustUI
    // draws between its header and credit strip, so the body reads as bands.
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
}

void FaustMeterPanel::resized() {
    if (entries_.empty())
        return;

    auto area = getLocalBounds().reduced(kPanelPadding, kPanelPadding);
    if (area.isEmpty())
        return;

    // Columns weighted by each meter's [width:N], so an author can give a
    // readout more room the same way they would a control.
    int totalWeight = 0;
    for (const auto& entry : entries_)
        totalWeight += juce::jmax(1, entry.info.widthCells);

    int consumed = 0;
    int placed = 0;
    for (auto& entry : entries_) {
        const int weight = juce::jmax(1, entry.info.widthCells);
        placed += weight;
        // Derive each edge from the running total rather than accumulating a
        // per-column width, so rounding cannot leave a gap at the right edge.
        const int right = (area.getWidth() * placed) / totalWeight;
        entry.widget->setBounds(area.getX() + consumed, area.getY(), right - consumed,
                                area.getHeight());
        consumed = right;
    }
}

}  // namespace magda::daw::ui
