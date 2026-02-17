#include "ChorusUI.hpp"

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

ChorusUI::ChorusUI() {
    setupSlider(depth_, "DEPTH");
    setupSlider(speed_, "SPEED");
    setupSlider(width_, "WIDTH");
    setupSlider(mix_, "MIX");

    // Depth: 0.1–20.0 ms
    depth_.slider.setRange(0.1, 20.0, 0.1);
    depth_.slider.setValueFormatter(
        [](double value) -> juce::String { return juce::String(value, 1) + " ms"; });
    depth_.slider.setValueParser([](const juce::String& text) -> double {
        return text.trim().replace("ms", "").trim().getDoubleValue();
    });
    depth_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(0, static_cast<float>(value));
    };

    // Speed: 0.1–10.0 Hz
    speed_.slider.setRange(0.1, 10.0, 0.1);
    speed_.slider.setValueFormatter(
        [](double value) -> juce::String { return juce::String(value, 1) + " Hz"; });
    speed_.slider.setValueParser([](const juce::String& text) -> double {
        return text.trim().replace("Hz", "").trim().getDoubleValue();
    });
    speed_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(1, static_cast<float>(value));
    };

    // Width: 0.0–1.0, displayed as percentage
    width_.slider.setRange(0.0, 1.0, 0.01);
    width_.slider.setValueFormatter([](double value) -> juce::String {
        return juce::String(static_cast<int>(100.0 * value)) + "%";
    });
    width_.slider.setValueParser([](const juce::String& text) -> double {
        return text.trim().replace("%", "").trim().getDoubleValue() / 100.0;
    });
    width_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(2, static_cast<float>(value));
    };

    // Mix: 0.0–1.0, displayed as "XX% wet"
    mix_.slider.setRange(0.0, 1.0, 0.01);
    mix_.slider.setValueFormatter([](double value) -> juce::String {
        return juce::String(juce::roundToInt(value * 100.0)) + "% wet";
    });
    mix_.slider.setValueParser([](const juce::String& text) -> double {
        return text.trim().replace("% wet", "").replace("%", "").trim().getDoubleValue() / 100.0;
    });
    mix_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(3, static_cast<float>(value));
    };
}

void ChorusUI::setupSlider(SliderWithLabel& s, const juce::String& labelText) {
    setupLabelStatic(s.label, labelText, this);
    addAndMakeVisible(s.slider);
}

void ChorusUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    if (params.size() > 0)
        depth_.slider.setValue(params[0].currentValue, juce::dontSendNotification);
    if (params.size() > 1)
        speed_.slider.setValue(params[1].currentValue, juce::dontSendNotification);
    if (params.size() > 2)
        width_.slider.setValue(params[2].currentValue, juce::dontSendNotification);
    if (params.size() > 3)
        mix_.slider.setValue(params[3].currentValue, juce::dontSendNotification);
}

void ChorusUI::paint(juce::Graphics& g) {
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(getLocalBounds().reduced(1));
}

void ChorusUI::resized() {
    auto area = getLocalBounds().reduced(6);
    int colWidth = area.getWidth() / 4;
    int labelHeight = 14;
    int sliderHeight = 18;

    int totalHeight = labelHeight + sliderHeight;
    auto row = area.withSizeKeepingCentre(area.getWidth(), totalHeight);

    SliderWithLabel* cols[] = {&depth_, &speed_, &width_, &mix_};
    for (auto* s : cols) {
        auto col = row.removeFromLeft(colWidth).reduced(2, 0);
        s->label.setBounds(col.removeFromTop(labelHeight));
        s->slider.setBounds(col.removeFromTop(sliderHeight));
    }
}

std::vector<LinkableTextSlider*> ChorusUI::getLinkableSliders() {
    return {&depth_.slider, &speed_.slider, &width_.slider, &mix_.slider};
}

}  // namespace magda::daw::ui
