#include "DelayUI.hpp"

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

DelayUI::DelayUI() {
    setupSlider(length_, "LENGTH");
    setupSlider(feedback_, "FEEDBACK");
    setupSlider(mix_, "MIX");

    // Length: 1–2000 ms, displayed as "XXX ms"
    // This is param index 2 (virtual, non-automatable but exposed by DelayProcessor)
    length_.slider.setRange(1.0, 2000.0, 1.0);
    length_.slider.setValueFormatter(
        [](double value) -> juce::String { return juce::String(static_cast<int>(value)) + " ms"; });
    length_.slider.setValueParser([](const juce::String& text) -> double {
        return text.trim().replace("ms", "").trim().getDoubleValue();
    });
    length_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(2, static_cast<float>(value));
    };

    // Feedback: -30.0 to 0.0 dB (uses Format::Decibels)
    feedback_.slider.setRange(-30.0, 0.0, 0.1);
    feedback_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(0, static_cast<float>(value));
    };

    // Mix proportion: 0.0–1.0, displayed as "XX% wet"
    mix_.slider.setRange(0.0, 1.0, 0.01);
    mix_.slider.setValueFormatter([](double value) -> juce::String {
        return juce::String(juce::roundToInt(value * 100.0)) + "% wet";
    });
    mix_.slider.setValueParser([](const juce::String& text) -> double {
        return text.trim().replace("% wet", "").replace("%", "").trim().getDoubleValue() / 100.0;
    });
    mix_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(1, static_cast<float>(value));
    };
}

void DelayUI::setupSlider(SliderWithLabel& s, const juce::String& labelText) {
    setupLabelStatic(s.label, labelText, this);
    addAndMakeVisible(s.slider);
}

void DelayUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    if (params.size() > 0)
        feedback_.slider.setValue(params[0].currentValue, juce::dontSendNotification);
    if (params.size() > 1)
        mix_.slider.setValue(params[1].currentValue, juce::dontSendNotification);
    if (params.size() > 2)
        length_.slider.setValue(params[2].currentValue, juce::dontSendNotification);
}

void DelayUI::paint(juce::Graphics& g) {
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(getLocalBounds().reduced(1));
}

void DelayUI::resized() {
    auto area = getLocalBounds().reduced(6);
    int colWidth = area.getWidth() / 3;
    int labelHeight = 14;
    int sliderHeight = 18;

    // Center vertically
    int totalHeight = labelHeight + sliderHeight;
    auto row = area.withSizeKeepingCentre(area.getWidth(), totalHeight);

    SliderWithLabel* cols[] = {&length_, &feedback_, &mix_};
    for (auto* s : cols) {
        auto col = row.removeFromLeft(colWidth).reduced(2, 0);
        s->label.setBounds(col.removeFromTop(labelHeight));
        s->slider.setBounds(col.removeFromTop(sliderHeight));
    }
}

std::vector<LinkableTextSlider*> DelayUI::getLinkableSliders() {
    return {&feedback_.slider, &mix_.slider, &length_.slider};
}

}  // namespace magda::daw::ui
