// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "custom_ui/SamplerEnvelopeView.hpp"

#include <cmath>

#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {
/// Each stage owns a fixed share of the width; its handle travels that share on a log scale.
constexpr float kAttackShare = 0.22f, kDecayShare = 0.26f, kHoldShare = 0.2f, kReleaseShare = 0.32f;
constexpr float kHandleRadius = 4.5f;
constexpr float kHandleGrab = 9.0f;

float stageFraction(const TextSlider& slider) {
    const double lo = slider.getMinimum(), hi = slider.getMaximum();
    return static_cast<float>(std::log(juce::jlimit(lo, hi, slider.getValue()) / lo) /
                              std::log(hi / lo));
}

double stageValue(const TextSlider& slider, float fraction) {
    const double lo = slider.getMinimum(), hi = slider.getMaximum();
    return lo * std::pow(hi / lo, static_cast<double>(juce::jlimit(0.0f, 1.0f, fraction)));
}
}  // namespace

void SamplerEnvelopeView::setStages(const Stages& stages) {
    stages_ = stages;
    dragging_ = Handle::None;
    repaint();
}

juce::Rectangle<float> SamplerEnvelopeView::plotArea() const {
    return getLocalBounds()
        .toFloat()
        .reduced(12.0f, 0.0f)
        .withTrimmedTop(12.0f)
        .withTrimmedBottom(18.0f);
}

bool SamplerEnvelopeView::inverted() const {
    return stages_.amount != nullptr && stages_.amount->getValue() < 0.0;
}

float SamplerEnvelopeView::levelAt(float y) const {
    const auto area = plotArea();
    const float fromBottom = (area.getBottom() - y) / area.getHeight();
    return juce::jlimit(0.0f, 1.0f, inverted() ? 1.0f - fromBottom : fromBottom);
}

std::optional<SamplerEnvelopeView::Shape> SamplerEnvelopeView::shape() const {
    if (stages_.attack == nullptr || stages_.decay == nullptr || stages_.sustain == nullptr ||
        stages_.release == nullptr || getLocalBounds().isEmpty())
        return std::nullopt;
    const auto area = plotArea();
    const float w = area.getWidth();
    const bool hanging = inverted();
    const auto y = [&](float level) {
        return hanging ? area.getY() + level * area.getHeight()
                       : area.getBottom() - level * area.getHeight();
    };
    const float x0 = area.getX();
    const float peakX = x0 + w * kAttackShare * stageFraction(stages_.attack->getSlider());
    const float decayX =
        x0 + w * kAttackShare + w * kDecayShare * stageFraction(stages_.decay->getSlider());
    const float holdX = x0 + w * (kAttackShare + kDecayShare + kHoldShare);
    const float endX = holdX + w * kReleaseShare * stageFraction(stages_.release->getSlider());
    const auto sustain = static_cast<float>(stages_.sustain->getValue());
    return Shape{{x0, y(0.0f)},
                 {peakX, y(1.0f)},
                 {decayX, y(sustain)},
                 {holdX, y(sustain)},
                 {endX, y(0.0f)}};
}

SamplerEnvelopeView::Handle SamplerEnvelopeView::handleAt(juce::Point<float> position) const {
    const auto points = shape();
    if (!points.has_value())
        return Handle::None;
    for (const auto& [handle, point] :
         {std::pair{Handle::Release, points->end}, std::pair{Handle::Decay, points->decayEnd},
          std::pair{Handle::Attack, points->peak}})
        if (position.getDistanceFrom(point) <= kHandleGrab)
            return handle;
    return Handle::None;
}

void SamplerEnvelopeView::drag(Handle handle, juce::Point<float> position) {
    const auto area = plotArea();
    const float w = area.getWidth();
    const auto within = [&](float start, float share) {
        return (position.x - start) / (w * share);
    };
    switch (handle) {
        case Handle::Attack:
            stages_.attack->setValue(
                stageValue(stages_.attack->getSlider(), within(area.getX(), kAttackShare)),
                juce::sendNotificationSync);
            break;
        case Handle::Decay:
            stages_.decay->setValue(stageValue(stages_.decay->getSlider(),
                                               within(area.getX() + w * kAttackShare, kDecayShare)),
                                    juce::sendNotificationSync);
            stages_.sustain->setValue(levelAt(position.y), juce::sendNotificationSync);
            break;
        case Handle::Release:
            stages_.release->setValue(
                stageValue(stages_.release->getSlider(),
                           within(area.getX() + w * (kAttackShare + kDecayShare + kHoldShare),
                                  kReleaseShare)),
                juce::sendNotificationSync);
            break;
        case Handle::None:
            break;
    }
    repaint();
}

void SamplerEnvelopeView::mouseDown(const juce::MouseEvent& e) {
    dragging_ = handleAt(e.position);
    repaint();
}

void SamplerEnvelopeView::mouseDrag(const juce::MouseEvent& e) {
    if (dragging_ != Handle::None)
        drag(dragging_, e.position);
}

void SamplerEnvelopeView::mouseUp(const juce::MouseEvent&) {
    if (dragging_ != Handle::None) {
        dragging_ = Handle::None;
        repaint();
    }
}

void SamplerEnvelopeView::mouseMove(const juce::MouseEvent& e) {
    setMouseCursor(handleAt(e.position) != Handle::None ? juce::MouseCursor::DraggingHandCursor
                                                        : juce::MouseCursor::NormalCursor);
}

void SamplerEnvelopeView::paint(juce::Graphics& g) {
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_BG));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    const auto points = shape();
    if (!points.has_value())
        return;
    const auto area = plotArea();
    // The stage boundaries: the handles never cross them.
    const float shares[] = {kAttackShare, kDecayShare, kHoldShare, kReleaseShare};
    float edges[5] = {area.getX()};
    for (int i = 0; i < 4; ++i)
        edges[i + 1] = edges[i] + shares[i] * area.getWidth();
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_GRID));
    for (int i = 1; i < 4; ++i)
        g.drawVerticalLine(juce::roundToInt(edges[i]), area.getY() - 4.0f, points->start.y);
    g.drawHorizontalLine(juce::roundToInt(points->start.y), area.getX(), area.getRight());

    const auto colour = ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE);
    if (stages_.amount != nullptr) {
        const auto amount = stages_.amount->getValue();
        g.setFont(FontManager::getInstance().getMonoFont(10.0f));
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
        g.drawText((amount > 0.0 ? "+" : "") + juce::String(static_cast<int>(amount)) + " st",
                   area.withHeight(14.0f).translated(0.0f, -10.0f),
                   juce::Justification::centredRight, false);
    }
    juce::Path path;
    path.startNewSubPath(points->start);
    for (const auto point : {points->peak, points->decayEnd, points->releaseStart, points->end})
        path.lineTo(point);
    juce::Path fill = path;
    fill.closeSubPath();
    g.setColour(colour.withAlpha(0.12f));
    g.fillPath(fill);
    g.setColour(colour.withAlpha(0.9f));
    g.strokePath(path, juce::PathStrokeType(1.5f));

    for (const auto& [handle, point] :
         {std::pair{Handle::Attack, points->peak}, std::pair{Handle::Decay, points->decayEnd},
          std::pair{Handle::Release, points->end}}) {
        const float radius = dragging_ == handle ? kHandleRadius + 1.5f : kHandleRadius;
        const auto dot = juce::Rectangle<float>(radius * 2, radius * 2).withCentre(point);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_BG));
        g.fillEllipse(dot);
        g.setColour(colour);
        g.drawEllipse(dot, 1.5f);
    }

    // Each stage named under its zone.
    g.setFont(FontManager::getInstance().getMonoFont(9.0f));
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
    const char* names[] = {"A", "D", "S", "R"};
    const float labelY = inverted() ? area.getBottom() + 2.0f : points->start.y + 2.0f;
    for (int i = 0; i < 4; ++i)
        g.drawText(names[i],
                   juce::Rectangle<float>(edges[i], labelY, edges[i + 1] - edges[i], 14.0f),
                   juce::Justification::centred, false);
}

}  // namespace magda::daw::ui
