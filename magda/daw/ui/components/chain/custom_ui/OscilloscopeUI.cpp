#include "OscilloscopeUI.hpp"

#include <algorithm>

namespace magda::daw::ui {

namespace {
constexpr int kControlRowH = 22;
}

OscilloscopeUI::OscilloscopeUI() {
    window_.assign(static_cast<size_t>(kMaxWindow), 0.0f);

    timeLabel_.setText("Time", juce::dontSendNotification);
    timeLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(timeLabel_);

    timeSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    timeSlider_.setRange(1.0, 1000.0, 0.0);
    timeSlider_.setSkewFactorFromMidPoint(40.0);  // log-ish: low end usable across 1-1000 ms
    timeSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58, 18);
    timeSlider_.setTextValueSuffix(" ms");
    timeSlider_.onValueChange = [this] {
        if (plugin_ != nullptr) {
            plugin_->setTimebaseMs(static_cast<float>(timeSlider_.getValue()));
            applyTimebase();
            repaint();
        }
    };
    addAndMakeVisible(timeSlider_);

    startTimerHz(30);
}

OscilloscopeUI::~OscilloscopeUI() {
    stopTimer();
}

void OscilloscopeUI::setPlugin(daw::audio::OscilloscopePlugin* plugin) {
    plugin_ = plugin;
    if (plugin_ == nullptr)
        return;
    timeSlider_.setValue(plugin_->getTimebaseMs(), juce::dontSendNotification);
    applyTimebase();
}

void OscilloscopeUI::applyTimebase() {
    const double sr = (plugin_ != nullptr) ? plugin_->getSampleRate() : 44100.0;
    const float ms = (plugin_ != nullptr) ? plugin_->getTimebaseMs() : 10.0f;
    const int samples = static_cast<int>(static_cast<double>(ms) * 0.001 * sr);
    displaySamples_ = juce::jlimit(64, kMaxWindow - kTriggerSearch, samples);
    readCount_ = juce::jmin(displaySamples_ + kTriggerSearch, kMaxWindow);
}

void OscilloscopeUI::resized() {
    auto controls = getLocalBounds().removeFromBottom(kControlRowH);
    timeLabel_.setBounds(controls.removeFromLeft(40));
    timeSlider_.setBounds(controls.reduced(2, 2));
}

void OscilloscopeUI::timerCallback() {
    if (plugin_ != nullptr)
        plugin_->getTapBuffer().readLatest(window_.data(), readCount_);
    repaint();
}

void OscilloscopeUI::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    bounds.removeFromBottom(kControlRowH);
    auto area = bounds.toFloat().reduced(4.0f);

    g.setColour(juce::Colour(0xff10141a));
    g.fillRoundedRectangle(area, 4.0f);

    const float midY = area.getCentreY();
    g.setColour(juce::Colour(0xff262c34));
    g.drawHorizontalLine(static_cast<int>(midY), area.getX(), area.getRight());

    if (static_cast<int>(window_.size()) < readCount_ || displaySamples_ < 2)
        return;

    // Rising zero-crossing trigger, searched in the head of the read so a full
    // displaySamples_ span is available after it.
    int trigger = 0;
    const int searchEnd = readCount_ - displaySamples_;
    for (int i = 1; i <= searchEnd; ++i) {
        if (window_[static_cast<size_t>(i - 1)] < 0.0f && window_[static_cast<size_t>(i)] >= 0.0f) {
            trigger = i;
            break;
        }
    }

    juce::Path path;
    const float w = area.getWidth();
    const float halfH = area.getHeight() * 0.5f;
    for (int i = 0; i < displaySamples_; ++i) {
        const float s = juce::jlimit(-1.0f, 1.0f, window_[static_cast<size_t>(trigger + i)]);
        const float x =
            area.getX() + w * (static_cast<float>(i) / static_cast<float>(displaySamples_ - 1));
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
