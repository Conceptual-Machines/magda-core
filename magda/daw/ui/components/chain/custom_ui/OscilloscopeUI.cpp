#include "OscilloscopeUI.hpp"

#include <algorithm>

namespace magda::daw::ui {

OscilloscopeUI::OscilloscopeUI() {
    window_.assign(static_cast<size_t>(kWindowSize), 0.0f);
    startTimerHz(30);
}

OscilloscopeUI::~OscilloscopeUI() {
    stopTimer();
}

void OscilloscopeUI::timerCallback() {
    if (plugin_ != nullptr)
        plugin_->getTapBuffer().readLatest(window_.data(), kWindowSize);
    repaint();
}

void OscilloscopeUI::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat().reduced(4.0f);

    g.setColour(juce::Colour(0xff10141a));
    g.fillRoundedRectangle(bounds, 4.0f);

    const float midY = bounds.getCentreY();
    g.setColour(juce::Colour(0xff262c34));
    g.drawHorizontalLine(static_cast<int>(midY), bounds.getX(), bounds.getRight());

    if (window_.size() < static_cast<size_t>(kWindowSize))
        return;

    // Rising zero-crossing trigger, searched in the head of the window so a
    // full kDisplaySamples span is always available after it.
    int trigger = 0;
    for (int i = 1; i <= kWindowSize - kDisplaySamples; ++i) {
        if (window_[static_cast<size_t>(i - 1)] < 0.0f && window_[static_cast<size_t>(i)] >= 0.0f) {
            trigger = i;
            break;
        }
    }

    juce::Path path;
    const float w = bounds.getWidth();
    const float halfH = bounds.getHeight() * 0.5f;
    for (int i = 0; i < kDisplaySamples; ++i) {
        const float s = juce::jlimit(-1.0f, 1.0f, window_[static_cast<size_t>(trigger + i)]);
        const float x =
            bounds.getX() + w * (static_cast<float>(i) / static_cast<float>(kDisplaySamples - 1));
        const float y = midY - s * halfH * 0.9f;
        if (i == 0)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);
    }

    g.setColour(juce::Colour(0xff4fd1c5));
    g.strokePath(path, juce::PathStrokeType(1.5f));
}

}  // namespace magda::daw::ui
