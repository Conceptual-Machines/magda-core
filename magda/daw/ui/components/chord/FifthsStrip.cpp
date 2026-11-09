// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "FifthsStrip.hpp"

#include <cmath>

#include "ui/components/chord/ChordWheel.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

using circle_of_fifths::Ring;
using magda::music::ChordQuality;
using magda::music::ChordRoot;

constexpr float kMinCellWidth = 46.0f;
constexpr float kMaxCellWidth = 72.0f;
constexpr int kMaxColumns = 11;
constexpr float kGap = 3.0f;
constexpr float kScrollPerStep = 0.1f;
constexpr int kDragThreshold = 4;

/** @brief Whole cells across @p width, odd so the chosen chord is the middle one. */
int columnsIn(float width) {
    const int columns = juce::jlimit(3, kMaxColumns, static_cast<int>(width / kMinCellWidth));
    return columns % 2 == 0 ? columns - 1 : columns;
}

}  // namespace

FifthsStrip::FifthsStrip() {
    setWantsKeyboardFocus(true);
}

void FifthsStrip::setSource(ChordRoot root, ChordQuality quality,
                            std::optional<chord_function::Key> key) {
    const bool minor = chord_function::hasMinorThird(quality);
    sourceRing_ = minor ? Ring::Inner : Ring::Outer;
    targetStep_ = circle_of_fifths::stepOf(root, minor);
    scroll_ = static_cast<float>(targetStep_);
    stopTimer();
    key_ = key;
    hovered_ = {};
    pressed_ = {};
    repaint();
}

float FifthsStrip::cellWidth() const {
    const auto width = static_cast<float>(getWidth());
    return std::min(kMaxCellWidth, width / static_cast<float>(columnsIn(width)));
}

juce::Rectangle<float> FifthsStrip::cellBounds(Ring ring, int unwrappedStep) const {
    const auto bounds = getLocalBounds().toFloat();
    const float width = cellWidth();
    const float rowHeight = (bounds.getHeight() - kGap) * 0.5f;
    const float x =
        bounds.getCentreX() + (static_cast<float>(unwrappedStep) - scroll_) * width - width * 0.5f;
    const float y = ring == Ring::Outer ? bounds.getY() : bounds.getY() + rowHeight + kGap;
    return {x + kGap * 0.5f, y, width - kGap, rowHeight};
}

circle_of_fifths::Segment FifthsStrip::segmentAt(juce::Point<float> position) const {
    const auto bounds = getLocalBounds().toFloat();
    const float width = cellWidth();
    const float band = width * static_cast<float>(columnsIn(bounds.getWidth()));
    if (std::abs(position.x - bounds.getCentreX()) > band * 0.5f || !bounds.contains(position))
        return {};
    const auto step =
        static_cast<int>(std::lround((position.x - bounds.getCentreX()) / width + scroll_));
    return {position.y < bounds.getCentreY() ? Ring::Outer : Ring::Inner,
            circle_of_fifths::wrap(step)};
}

void FifthsStrip::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    const int columns = columnsIn(bounds.getWidth());
    const float band = cellWidth() * static_cast<float>(columns);
    const juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(bounds.withSizeKeepingCentre(band, bounds.getHeight()).toNearestInt());

    const auto highlight = ActiveTheme::getColour(ActiveTheme::DEVICE_AMBER);
    const Segment target{sourceRing_, circle_of_fifths::wrap(targetStep_)};
    auto& fonts = FontManager::getInstance();
    const int first = static_cast<int>(std::floor(scroll_)) - columns / 2 - 1;
    const int last = static_cast<int>(std::ceil(scroll_)) + columns / 2 + 1;

    for (const auto ring : {Ring::Outer, Ring::Inner}) {
        const auto base =
            ActiveTheme::getColour(ActiveTheme::SURFACE).darker(ring == Ring::Outer ? 0.0f : 0.25f);
        for (int step = first; step <= last; ++step) {
            const auto cell = cellBounds(ring, step);
            const Segment segment{ring, circle_of_fifths::wrap(step)};
            auto fill = base;
            if (segment == pressed_)
                fill = base.brighter(0.35f);
            else if (segment == hovered_)
                fill = ActiveTheme::getColour(ActiveTheme::SURFACE_HOVER);
            if (segment == target)
                fill = fill.interpolatedWith(highlight, 0.35f);
            g.setColour(fill);
            g.fillRoundedRectangle(cell, 4.0f);
            if (segment == target) {
                g.setColour(highlight);
                g.drawRoundedRectangle(cell.reduced(0.5f), 4.0f, 1.2f);
            }

            const auto root = circle_of_fifths::rootAt(segment);
            const auto quality = circle_of_fifths::qualityAt(segment);
            const float nameSize = juce::jlimit(10.0f, 15.0f, cell.getHeight() * 0.32f);
            const auto degree = key_.has_value() && cell.getHeight() >= 30.0f
                                    ? chord_function::romanNumeral(root, quality, *key_)
                                    : juce::String();
            auto text = cell.withSizeKeepingCentre(
                cell.getWidth(), nameSize * 1.25f + (degree.isNotEmpty() ? nameSize : 0.0f));
            g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
            g.setFont(fonts.getUIFontMedium(nameSize));
            g.drawText(ChordWheel::labelFor(root, quality), text.removeFromTop(nameSize * 1.25f),
                       juce::Justification::centred, false);
            if (degree.isNotEmpty()) {
                g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
                g.setFont(fonts.getUIFont(nameSize * 0.75f));
                g.drawText(degree, text, juce::Justification::centred, false);
            }
        }
    }
}

void FifthsStrip::mouseMove(const juce::MouseEvent& e) {
    if (const auto segment = segmentAt(e.position); segment != hovered_) {
        hovered_ = segment;
        repaint();
    }
}

void FifthsStrip::mouseExit(const juce::MouseEvent&) {
    hovered_ = {};
    repaint();
}

void FifthsStrip::mouseDown(const juce::MouseEvent& e) {
    dragging_ = false;
    pressed_ = segmentAt(e.position);
    if (pressed_.ring != Ring::None && onAuditionStart)
        onAuditionStart(circle_of_fifths::rootAt(pressed_), circle_of_fifths::qualityAt(pressed_));
    repaint();
}

void FifthsStrip::mouseDrag(const juce::MouseEvent& e) {
    if (pressed_.ring == Ring::None)
        return;
    if (!dragging_) {
        if (e.getDistanceFromDragStart() <= kDragThreshold)
            return;
        dragging_ = true;
        if (onAuditionStop)
            onAuditionStop();
    }
    if (onDragMove)
        onDragMove(circle_of_fifths::rootAt(pressed_), circle_of_fifths::qualityAt(pressed_));
}

void FifthsStrip::mouseUp(const juce::MouseEvent&) {
    const auto pressed = pressed_;
    const bool dragged = dragging_;
    pressed_ = {};
    dragging_ = false;
    repaint();
    if (pressed.ring != Ring::None && !dragged && onAuditionStop)
        onAuditionStop();
}

void FifthsStrip::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) {
    scrollAccumulator_ -=
        std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? -wheel.deltaX : wheel.deltaY;
    const int steps = static_cast<int>(scrollAccumulator_ / kScrollPerStep);
    if (steps != 0) {
        scrollAccumulator_ -= static_cast<float>(steps) * kScrollPerStep;
        scrollBy(steps);
    }
}

bool FifthsStrip::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey) {
        scrollBy(key == juce::KeyPress::leftKey ? -1 : 1);
        return true;
    }
    return false;
}

void FifthsStrip::scrollBy(int steps) {
    targetStep_ += steps;
    startTimerHz(60);
    repaint();
}

void FifthsStrip::timerCallback() {
    const float remaining = static_cast<float>(targetStep_) - scroll_;
    if (std::abs(remaining) < 0.01f) {
        scroll_ = static_cast<float>(targetStep_);
        stopTimer();
    } else {
        scroll_ += remaining * 0.3f;
    }
    repaint();
}

}  // namespace magda::daw::ui
