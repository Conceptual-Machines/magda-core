#include "PhaserUI.hpp"

#include <juce_audio_basics/juce_audio_basics.h>

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

static void setupLabelStatic(juce::Label& label, const juce::String& text,
                             juce::Component* parent) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(FontManager::getInstance().getUIFont(9.0f));
    label.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    label.setJustificationType(juce::Justification::centred);
    parent->addAndMakeVisible(label);
}

PhaserUI::PhaserUI() {
    setupSlider(depth_, "DEPTH");
    setupSlider(rate_, "RATE");
    setupSlider(feedback_, "FEEDBACK");

    // Depth: 0.0–12.0 (octave range for sweep)
    depth_.slider.setRange(0.0, 12.0, 0.1);
    depth_.slider.setValueFormatter(
        [](double value) -> juce::String { return juce::String(value, 1); });
    depth_.slider.setValueParser(
        [](const juce::String& text) -> double { return text.trim().getDoubleValue(); });
    depth_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(0, static_cast<float>(value));
    };

    // Rate: 0.0–2.0 (sweep speed)
    rate_.slider.setRange(0.0, 2.0, 0.01);
    rate_.slider.setValueFormatter(
        [](double value) -> juce::String { return juce::String(value, 2); });
    rate_.slider.setValueParser(
        [](const juce::String& text) -> double { return text.trim().getDoubleValue(); });
    rate_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(1, static_cast<float>(value));
    };

    // Feedback: 0.0–0.99 (must be <1.0 for stability)
    feedback_.slider.setRange(0.0, 0.99, 0.01);
    feedback_.slider.setValueFormatter([](double value) -> juce::String {
        return juce::String(static_cast<int>(100.0 * value)) + "%";
    });
    feedback_.slider.setValueParser([](const juce::String& text) -> double {
        return text.trim().replace("%", "").trim().getDoubleValue() / 100.0;
    });
    feedback_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(2, static_cast<float>(value));
    };
}

void PhaserUI::setupSlider(SliderWithLabel& s, const juce::String& labelText) {
    setupLabelStatic(s.label, labelText, this);
    addAndMakeVisible(s.slider);
}

void PhaserUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    if (params.size() > 0)
        depth_.slider.setValue(params[0].currentValue, juce::dontSendNotification);
    if (params.size() > 1)
        rate_.slider.setValue(params[1].currentValue, juce::dontSendNotification);
    if (params.size() > 2)
        feedback_.slider.setValue(params[2].currentValue, juce::dontSendNotification);
}

void PhaserUI::paint(juce::Graphics& g) {
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(getLocalBounds().reduced(1));
}

void PhaserUI::resized() {
    auto area = getLocalBounds().reduced(6);
    int colWidth = area.getWidth() / 3;
    int labelHeight = 14;
    int sliderHeight = 18;

    int totalHeight = labelHeight + sliderHeight;
    auto row = area.withSizeKeepingCentre(area.getWidth(), totalHeight);

    SliderWithLabel* cols[] = {&depth_, &rate_, &feedback_};
    for (auto* s : cols) {
        auto col = row.removeFromLeft(colWidth).reduced(2, 0);
        s->label.setBounds(col.removeFromTop(labelHeight));
        s->slider.setBounds(col.removeFromTop(sliderHeight));
    }
}

std::vector<LinkableTextSlider*> PhaserUI::getLinkableSliders() {
    return {&depth_.slider, &rate_.slider, &feedback_.slider};
}

}  // namespace magda::daw::ui
