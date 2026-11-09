// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ChordWheel.hpp"

#include <cmath>
#include <numbers>

#include "music/NotationSettings.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

using circle_of_fifths::Ring;
using magda::music::ChordQuality;
using magda::music::ChordRoot;

constexpr float kStepAngle = 2.0f * std::numbers::pi_v<float> / circle_of_fifths::kSegments;
constexpr float kShadowMargin = 12.0f;
/// A scroll of this much turns the wheel one step.
constexpr float kScrollPerStep = 0.1f;
/// Pixels a press moves before it is a drag rather than a click.
constexpr int kDragThreshold = 4;

juce::Point<float> polar(juce::Point<float> centre, float radius, float clockwiseFromTop) {
    return centre.translated(radius * std::sin(clockwiseFromTop),
                             -radius * std::cos(clockwiseFromTop));
}

}  // namespace

ChordWheel::ChordWheel() {
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);
}

void ChordWheel::setSource(ChordRoot root, ChordQuality quality, const juce::String& name,
                           std::optional<chord_function::Key> key) {
    const bool minor = chord_function::hasMinorThird(quality);
    sourceRing_ = minor ? Ring::Inner : Ring::Outer;
    targetStep_ = circle_of_fifths::stepOf(root, minor);
    rotation_ = static_cast<float>(targetStep_);
    stopTimer();
    sourceName_ = name;
    key_ = key;
    sourceDegree_ = key.has_value() ? chord_function::romanNumeral(root, quality, *key) : "";
    hovered_ = {};
    pressed_ = {};
    repaint();
}

juce::String ChordWheel::labelFor(ChordRoot root, ChordQuality quality) {
    const auto name =
        magda::music::ChordUtils::rootToString(root) + (quality == ChordQuality::Minor ? "m" : "");
    return magda::music::NotationSettings::getInstance().formatRoot(name);
}

circle_of_fifths::Radii ChordWheel::radii() const {
    const float outer =
        static_cast<float>(juce::jmin(getWidth(), getHeight())) * 0.5f - kShadowMargin;
    return {outer * 0.36f, outer * 0.68f, outer};
}

circle_of_fifths::Segment ChordWheel::segmentAt(juce::Point<float> position) const {
    const auto offset = position - getLocalBounds().toFloat().getCentre();
    return circle_of_fifths::segmentAt(offset.x, offset.y, rotation_, radii());
}

circle_of_fifths::Segment ChordWheel::topSegment() const {
    return {sourceRing_, circle_of_fifths::wrap(targetStep_)};
}

void ChordWheel::paint(juce::Graphics& g) {
    const auto centre = getLocalBounds().toFloat().getCentre();
    const float outer = radii().outer;

    juce::Path disc;
    disc.addEllipse(juce::Rectangle<float>(outer * 2.0f, outer * 2.0f).withCentre(centre));
    juce::DropShadow(juce::Colours::black.withAlpha(0.45f), static_cast<int>(kShadowMargin), {})
        .drawForPath(g, disc);

    paintRing(g, Ring::Outer);
    paintRing(g, Ring::Inner);
    paintHub(g);
}

void ChordWheel::paintRing(juce::Graphics& g, Ring ring) const {
    const auto r = radii();
    const auto centre = getLocalBounds().toFloat().getCentre();
    const bool outerRing = ring == Ring::Outer;
    const float from = outerRing ? r.inner : r.hub;
    const float to = outerRing ? r.outer : r.inner;
    const auto bounds = juce::Rectangle<float>(to * 2.0f, to * 2.0f).withCentre(centre);

    const auto base = ActiveTheme::getColour(ActiveTheme::SURFACE).darker(outerRing ? 0.0f : 0.25f);
    const auto highlight = ActiveTheme::getColour(ActiveTheme::DEVICE_AMBER);
    const auto gap = ActiveTheme::getColour(ActiveTheme::BACKGROUND);
    auto& fonts = FontManager::getInstance();
    const float nameSize = r.outer * (outerRing ? 0.105f : 0.09f);
    const float degreeSize = nameSize * 0.72f;

    for (int step = 0; step < circle_of_fifths::kSegments; ++step) {
        const Segment segment{ring, step};
        const float angle = (static_cast<float>(step) - rotation_) * kStepAngle;
        const bool top = segment == topSegment();

        juce::Path wedge;
        wedge.addPieSegment(bounds, angle - kStepAngle * 0.5f, angle + kStepAngle * 0.5f,
                            from / to);
        auto fill = base;
        if (segment == pressed_)
            fill = base.brighter(0.35f);
        else if (segment == hovered_)
            fill = ActiveTheme::getColour(ActiveTheme::SURFACE_HOVER);
        if (top)
            fill = fill.interpolatedWith(highlight, 0.35f);
        g.setColour(fill);
        g.fillPath(wedge);
        g.setColour(gap);
        g.strokePath(wedge, juce::PathStrokeType(2.0f));
        if (top) {
            g.setColour(highlight);
            g.strokePath(wedge, juce::PathStrokeType(1.5f));
        }

        const auto root = circle_of_fifths::rootAt(segment);
        const auto quality = circle_of_fifths::qualityAt(segment);
        const auto degree =
            key_.has_value() ? chord_function::romanNumeral(root, quality, *key_) : juce::String();
        const auto labelCentre = polar(centre, (from + to) * 0.5f, angle);
        const float lineHeight = nameSize * 1.2f;
        const float stackHeight = lineHeight + (degree.isNotEmpty() ? degreeSize * 1.2f : 0.0f);
        auto label = juce::Rectangle<float>(to - from, stackHeight).withCentre(labelCentre);

        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
        g.setFont(fonts.getUIFontMedium(nameSize));
        g.drawText(labelFor(root, quality), label.removeFromTop(lineHeight),
                   juce::Justification::centred, false);
        if (degree.isNotEmpty()) {
            g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
            g.setFont(fonts.getUIFont(degreeSize));
            g.drawText(degree, label, juce::Justification::centred, false);
        }
    }
}

void ChordWheel::paintHub(juce::Graphics& g) const {
    const auto r = radii();
    const auto centre = getLocalBounds().toFloat().getCentre();
    const auto hub = juce::Rectangle<float>(r.hub * 2.0f, r.hub * 2.0f).withCentre(centre);
    g.setColour(ActiveTheme::getColour(ActiveTheme::BACKGROUND));
    g.fillEllipse(hub.reduced(1.0f));
    g.setColour(ActiveTheme::getColour(ActiveTheme::BORDER));
    g.drawEllipse(hub.reduced(1.0f), 1.0f);

    auto& fonts = FontManager::getInstance();
    const float nameSize = r.outer * 0.12f;
    const float smallSize = r.outer * 0.07f;
    auto text = juce::Rectangle<float>(r.hub * 1.7f, nameSize * 1.25f + smallSize * 3.0f)
                    .withCentre(centre);

    g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
    g.setFont(fonts.getUIFontBold(smallSize).withExtraKerningFactor(0.15f));
    g.drawText("FROM", text.removeFromTop(smallSize * 1.4f), juce::Justification::centred, false);
    g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
    g.setFont(fonts.getUIFontMedium(nameSize));
    g.drawFittedText(sourceName_, text.removeFromTop(nameSize * 1.25f).toNearestInt(),
                     juce::Justification::centred, 1, 0.8f);
    if (sourceDegree_.isNotEmpty()) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY));
        g.setFont(fonts.getUIFont(smallSize * 1.3f));
        g.drawText(sourceDegree_, text, juce::Justification::centredTop, false);
    }
}

bool ChordWheel::hitTest(int x, int y) {
    const auto offset = juce::Point<float>(static_cast<float>(x), static_cast<float>(y)) -
                        getLocalBounds().toFloat().getCentre();
    return offset.getDistanceFromOrigin() < radii().outer;
}

void ChordWheel::mouseMove(const juce::MouseEvent& e) {
    if (const auto segment = segmentAt(e.position); segment != hovered_) {
        hovered_ = segment;
        repaint();
    }
}

void ChordWheel::mouseExit(const juce::MouseEvent&) {
    hovered_ = {};
    repaint();
}

void ChordWheel::mouseDown(const juce::MouseEvent& e) {
    // A drag handed to the OS may never have brought its mouseUp back here.
    dragging_ = false;
    pressed_ = segmentAt(e.position);
    if (pressed_.ring != Ring::None && onAuditionStart)
        onAuditionStart(circle_of_fifths::rootAt(pressed_), circle_of_fifths::qualityAt(pressed_));
    repaint();
}

void ChordWheel::mouseDrag(const juce::MouseEvent& e) {
    if (pressed_.ring == Ring::None)
        return;
    if (!dragging_) {
        if (e.getDistanceFromDragStart() <= kDragThreshold)
            return;
        dragging_ = true;
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        if (onAuditionStop)
            onAuditionStop();
    }
    if (onDragMove)
        onDragMove(circle_of_fifths::rootAt(pressed_), circle_of_fifths::qualityAt(pressed_),
                   e.getPosition());
}

void ChordWheel::mouseUp(const juce::MouseEvent& e) {
    const auto pressed = pressed_;
    const bool dragged = dragging_;
    pressed_ = {};
    dragging_ = false;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
    if (pressed.ring == Ring::None)
        return;
    if (!dragged) {
        if (onAuditionStop)
            onAuditionStop();
    } else if (onDragEnd) {
        onDragEnd(circle_of_fifths::rootAt(pressed), circle_of_fifths::qualityAt(pressed),
                  e.getPosition());
    }
}

void ChordWheel::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) {
    scrollAccumulator_ -= wheel.deltaY;
    const int steps = static_cast<int>(scrollAccumulator_ / kScrollPerStep);
    if (steps != 0) {
        scrollAccumulator_ -= static_cast<float>(steps) * kScrollPerStep;
        rotateBy(steps);
    }
}

bool ChordWheel::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::leftKey) {
        rotateBy(-1);
        return true;
    }
    if (key == juce::KeyPress::rightKey) {
        rotateBy(1);
        return true;
    }
    if (key == juce::KeyPress::returnKey) {
        if (onPick)
            onPick(circle_of_fifths::rootAt(topSegment()),
                   circle_of_fifths::qualityAt(topSegment()));
        return true;
    }
    if (key == juce::KeyPress::escapeKey) {
        if (onDismiss)
            onDismiss();
        return true;
    }
    return false;
}

void ChordWheel::rotateBy(int steps) {
    targetStep_ += steps;
    startTimerHz(60);
    repaint();
}

void ChordWheel::timerCallback() {
    const float remaining = static_cast<float>(targetStep_) - rotation_;
    if (std::abs(remaining) < 0.01f) {
        rotation_ = static_cast<float>(targetStep_);
        stopTimer();
    } else {
        rotation_ += remaining * 0.3f;
    }
    repaint();
}

}  // namespace magda::daw::ui
