#include "BarsBeatsTicksLabel.hpp"

#include <cmath>

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda {

// =============================================================================
// BarsBeatsTicksLabel
// =============================================================================

BarsBeatsTicksLabel::BarsBeatsTicksLabel() {
    barsSegment_ = std::make_unique<SegmentLabel>(*this, SegmentType::Bars);
    beatsSegment_ = std::make_unique<SegmentLabel>(*this, SegmentType::Beats);
    ticksSegment_ = std::make_unique<SegmentLabel>(*this, SegmentType::Ticks);

    addAndMakeVisible(*barsSegment_);
    addAndMakeVisible(*beatsSegment_);
    addAndMakeVisible(*ticksSegment_);

    updateSegmentTexts();
}

BarsBeatsTicksLabel::~BarsBeatsTicksLabel() = default;

void BarsBeatsTicksLabel::setRange(double min, double max, double defaultValue) {
    minValue_ = min;
    maxValue_ = max;
    defaultValue_ = juce::jlimit(min, max, defaultValue);
    value_ = juce::jlimit(minValue_, maxValue_, value_);
    updateSegmentTexts();
    repaint();
}

void BarsBeatsTicksLabel::setValue(double newValue, juce::NotificationType notification) {
    newValue = juce::jlimit(minValue_, maxValue_, newValue);
    if (std::abs(newValue - value_) > 0.0001) {
        value_ = newValue;
        updateSegmentTexts();
        repaint();
        if (notification != juce::dontSendNotification && onValueChange) {
            onValueChange();
        }
    }
}

void BarsBeatsTicksLabel::setBeatsPerBar(int beatsPerBar) {
    beatsPerBar_ = beatsPerBar;
    updateSegmentTexts();
    repaint();
}

void BarsBeatsTicksLabel::setBarsBeatsIsPosition(bool isPosition) {
    barsBeatsIsPosition_ = isPosition;
    updateSegmentTexts();
    repaint();
}

void BarsBeatsTicksLabel::decompose(int& bars, int& beats, int& ticks) const {
    double v = value_;
    if (v < 0.0)
        v = 0.0;

    bars = static_cast<int>(v / beatsPerBar_);
    double remaining = std::fmod(v, static_cast<double>(beatsPerBar_));
    if (remaining < 0.0)
        remaining = 0.0;

    beats = static_cast<int>(remaining);
    ticks = static_cast<int>(std::round((remaining - beats) * TICKS_PER_BEAT));
    if (ticks >= TICKS_PER_BEAT) {
        ticks = 0;
        beats++;
        if (beats >= beatsPerBar_) {
            beats = 0;
            bars++;
        }
    }
}

double BarsBeatsTicksLabel::recompose(int bars, int beats, int ticks) const {
    return bars * beatsPerBar_ + beats + ticks / static_cast<double>(TICKS_PER_BEAT);
}

void BarsBeatsTicksLabel::onSegmentChanged() {
    int offset = barsBeatsIsPosition_ ? 1 : 0;
    int bars = barsSegment_->getDisplayValue() - offset;
    int beats = beatsSegment_->getDisplayValue() - offset;
    int ticks = ticksSegment_->getDisplayValue();

    if (bars < 0)
        bars = 0;
    if (beats < 0)
        beats = 0;
    if (ticks < 0)
        ticks = 0;

    double newValue = recompose(bars, beats, ticks);
    setValue(newValue);
}

void BarsBeatsTicksLabel::updateSegmentTexts() {
    int bars, beats, ticks;
    decompose(bars, beats, ticks);

    int offset = barsBeatsIsPosition_ ? 1 : 0;
    barsSegment_->setDisplayValue(bars + offset);
    beatsSegment_->setDisplayValue(beats + offset);
    ticksSegment_->setDisplayValue(ticks);
}

void BarsBeatsTicksLabel::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();

    // Background
    g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
    g.fillRoundedRectangle(bounds, 2.0f);

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 2.0f, 1.0f);

    // Draw dot separators between segments
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    g.setFont(FontManager::getInstance().getUIFont(10.0f));

    // Dot between bars and beats
    auto dot1X = barsSegment_->getRight();
    auto dot2X = beatsSegment_->getRight();
    float dotY = bounds.getCentreY();
    float dotRadius = 1.5f;

    float dot1CenterX =
        static_cast<float>(dot1X) +
        (static_cast<float>(beatsSegment_->getX()) - static_cast<float>(dot1X)) * 0.5f;
    g.fillEllipse(dot1CenterX - dotRadius, dotY - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f);

    float dot2CenterX =
        static_cast<float>(dot2X) +
        (static_cast<float>(ticksSegment_->getX()) - static_cast<float>(dot2X)) * 0.5f;
    g.fillEllipse(dot2CenterX - dotRadius, dotY - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f);
}

void BarsBeatsTicksLabel::resized() {
    auto bounds = getLocalBounds().reduced(2, 0);
    int totalWidth = bounds.getWidth();

    // Proportions: bars ~25%, dot ~5%, beats ~25%, dot ~5%, ticks ~40%
    int dotWidth = 6;
    int availableWidth = totalWidth - dotWidth * 2;
    int barsWidth = static_cast<int>(availableWidth * 0.25);
    int beatsWidth = static_cast<int>(availableWidth * 0.25);
    int ticksWidth = availableWidth - barsWidth - beatsWidth;

    int x = bounds.getX();
    barsSegment_->setBounds(x, bounds.getY(), barsWidth, bounds.getHeight());
    x += barsWidth + dotWidth;
    beatsSegment_->setBounds(x, bounds.getY(), beatsWidth, bounds.getHeight());
    x += beatsWidth + dotWidth;
    ticksSegment_->setBounds(x, bounds.getY(), ticksWidth, bounds.getHeight());
}

// =============================================================================
// SegmentLabel
// =============================================================================

BarsBeatsTicksLabel::SegmentLabel::SegmentLabel(BarsBeatsTicksLabel& owner, SegmentType type)
    : owner_(owner), type_(type) {
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

BarsBeatsTicksLabel::SegmentLabel::~SegmentLabel() = default;

void BarsBeatsTicksLabel::SegmentLabel::setDisplayValue(int val) {
    displayValue_ = val;
    repaint();
}

juce::String BarsBeatsTicksLabel::SegmentLabel::formatDisplay() const {
    if (type_ == SegmentType::Ticks) {
        // Zero-padded to 3 digits
        char buffer[8];
        std::snprintf(buffer, sizeof(buffer), "%03d", displayValue_);
        return juce::String(buffer);
    }
    return juce::String(displayValue_);
}

void BarsBeatsTicksLabel::SegmentLabel::paint(juce::Graphics& g) {
    if (!isEditing_) {
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.drawText(formatDisplay(), getLocalBounds(), juce::Justification::centred, false);
    }
}

void BarsBeatsTicksLabel::SegmentLabel::mouseDown(const juce::MouseEvent& e) {
    if (isEditing_ || !owner_.isEnabled())
        return;

    isDragging_ = true;
    dragStartY_ = e.y;
    dragAccumulator_ = 0.0;
    repaint();
}

void BarsBeatsTicksLabel::SegmentLabel::mouseDrag(const juce::MouseEvent& e) {
    if (!isDragging_)
        return;

    int deltaY = dragStartY_ - e.y;  // up = positive
    double pixelsPerStep = getDragPixelsPerStep();
    bool shift = e.mods.isShiftDown();

    // Accumulate fractional steps
    dragAccumulator_ += deltaY / pixelsPerStep;
    dragStartY_ = e.y;

    int steps = static_cast<int>(dragAccumulator_);
    if (steps == 0)
        return;
    dragAccumulator_ -= steps;

    double increment = getDefaultIncrement(shift);
    double delta = steps * increment;

    double newValue = juce::jlimit(owner_.minValue_, owner_.maxValue_, owner_.value_ + delta);
    owner_.setValue(newValue);
}

void BarsBeatsTicksLabel::SegmentLabel::mouseUp(const juce::MouseEvent&) {
    isDragging_ = false;
}

void BarsBeatsTicksLabel::SegmentLabel::mouseDoubleClick(const juce::MouseEvent&) {
    if (!owner_.isEnabled())
        return;
    if (owner_.doubleClickResets_) {
        owner_.setValue(owner_.defaultValue_);
    } else {
        startEditing();
    }
}

void BarsBeatsTicksLabel::SegmentLabel::mouseWheelMove(const juce::MouseEvent& e,
                                                       const juce::MouseWheelDetails& wheel) {
    if (isEditing_ || !isEnabled())
        return;

    bool shift = e.mods.isShiftDown();
    double increment = getDefaultIncrement(shift);
    double direction = (wheel.deltaY > 0) ? 1.0 : -1.0;

    double newValue =
        juce::jlimit(owner_.minValue_, owner_.maxValue_, owner_.value_ + increment * direction);
    owner_.setValue(newValue);
}

double BarsBeatsTicksLabel::SegmentLabel::getDefaultIncrement(bool shift) const {
    switch (type_) {
        case SegmentType::Bars:
            return shift ? 1.0 : static_cast<double>(owner_.beatsPerBar_);
        case SegmentType::Beats:
            return shift ? 0.25 : 1.0;
        case SegmentType::Ticks:
            return shift ? (1.0 / TICKS_PER_BEAT)
                         : (static_cast<double>(TICKS_PER_16TH) / TICKS_PER_BEAT);
        default:
            return 1.0;
    }
}

double BarsBeatsTicksLabel::SegmentLabel::getDragPixelsPerStep() const {
    switch (type_) {
        case SegmentType::Bars:
            return 30.0;
        case SegmentType::Beats:
            return 30.0;
        case SegmentType::Ticks:
            return 20.0;
        default:
            return 30.0;
    }
}

void BarsBeatsTicksLabel::SegmentLabel::startEditing() {
    if (isEditing_)
        return;

    isEditing_ = true;

    editor_ = std::make_unique<juce::TextEditor>();
    editor_->setBounds(getLocalBounds());
    editor_->setFont(FontManager::getInstance().getUIFont(10.0f));
    editor_->setText(formatDisplay(), false);
    editor_->selectAll();
    editor_->setJustification(juce::Justification::centred);
    editor_->setColour(juce::TextEditor::backgroundColourId,
                       DarkTheme::getColour(DarkTheme::SURFACE));
    editor_->setColour(juce::TextEditor::textColourId,
                       DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    editor_->setColour(juce::TextEditor::highlightColourId,
                       DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    editor_->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    editor_->setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);

    editor_->onReturnKey = [this]() { finishEditing(); };
    editor_->onEscapeKey = [this]() { cancelEditing(); };
    editor_->onFocusLost = [this]() { finishEditing(); };

    addAndMakeVisible(*editor_);
    editor_->grabKeyboardFocus();
    repaint();
}

void BarsBeatsTicksLabel::SegmentLabel::finishEditing() {
    if (!isEditing_ || !editor_)
        return;

    int newVal = editor_->getText().getIntValue();
    isEditing_ = false;
    editor_ = nullptr;

    displayValue_ = newVal;
    owner_.onSegmentChanged();
    repaint();
}

void BarsBeatsTicksLabel::SegmentLabel::cancelEditing() {
    if (!isEditing_)
        return;

    isEditing_ = false;
    editor_ = nullptr;
    repaint();
}

}  // namespace magda
