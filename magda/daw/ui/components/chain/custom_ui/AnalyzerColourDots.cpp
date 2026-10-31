// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AnalyzerColourDots.hpp"

#include "AnalyzerColours.hpp"
#include "ui/themes/ActiveTheme.hpp"

namespace magda::daw::ui {

namespace {
constexpr float kDot = 10.0f;
constexpr float kPitch = 16.0f;  // dot plus the gap the ring needs
}  // namespace

void AnalyzerColourDots::setSelectedIndex(int index) {
    index = juce::jlimit(0, kAnalyzerColourCount - 1, index);
    if (index == selected_)
        return;
    selected_ = index;
    repaint();
}

int AnalyzerColourDots::getPreferredWidth() const {
    return juce::roundToInt(kPitch * kAnalyzerColourCount);
}

juce::Rectangle<float> AnalyzerColourDots::dotBounds(int index) const {
    const float x = (static_cast<float>(index) + 0.5f) * kPitch;
    return juce::Rectangle<float>(kDot, kDot)
        .withCentre({x, static_cast<float>(getHeight()) * 0.5f});
}

int AnalyzerColourDots::indexAt(juce::Point<int> point) const {
    const int index = static_cast<int>(static_cast<float>(point.x) / kPitch);
    return index >= 0 && index < kAnalyzerColourCount ? index : -1;
}

void AnalyzerColourDots::paint(juce::Graphics& g) {
    for (int i = 0; i < kAnalyzerColourCount; ++i) {
        const auto dot = dotBounds(i);
        g.setColour(
            analyzerTraceColour(i).withAlpha(i == selected_ || i == hovered_ ? 1.0f : 0.6f));
        g.fillEllipse(dot);
        if (i == selected_) {
            g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_VALUE_TEXT));
            g.drawEllipse(dot.expanded(2.5f), 1.2f);
        }
    }
}

void AnalyzerColourDots::mouseDown(const juce::MouseEvent& e) {
    const int index = indexAt(e.getPosition());
    if (index < 0 || index == selected_)
        return;
    setSelectedIndex(index);
    if (onChange)
        onChange(index);
}

void AnalyzerColourDots::mouseMove(const juce::MouseEvent& e) {
    const int index = indexAt(e.getPosition());
    if (index != hovered_) {
        hovered_ = index;
        repaint();
    }
}

void AnalyzerColourDots::mouseExit(const juce::MouseEvent&) {
    hovered_ = -1;
    repaint();
}

juce::String AnalyzerColourDots::getTooltip() {
    const int index = indexAt(getMouseXYRelative());
    return index >= 0 ? juce::String(kAnalyzerColourNames[index]) : juce::String("Trace colour");
}

}  // namespace magda::daw::ui
