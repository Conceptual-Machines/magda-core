#include "CompressorUI.hpp"

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

CompressorUI::CompressorUI() {
    setupSlider(threshold_, "THRESH");
    setupSlider(ratio_, "RATIO");
    setupSlider(attack_, "ATTACK");
    setupSlider(release_, "REL");
    setupSlider(output_, "GAIN");
    setupSlider(sidechain_, "SC GAIN");

    // Threshold: linear gain 0.01–1.0, displayed as dB
    threshold_.slider.setRange(0.01, 1.0, 0.001);
    threshold_.slider.setValueFormatter([](double value) -> juce::String {
        float db = juce::Decibels::gainToDecibels(static_cast<float>(value), -40.0f);
        if (db <= -40.0f)
            return "-inf dB";
        return juce::String(db, 1) + " dB";
    });
    threshold_.slider.setValueParser([](const juce::String& text) -> double {
        auto t = text.trim().toLowerCase();
        if (t.contains("inf"))
            return 0.01;
        t = t.replace("db", "").trim();
        float db = t.getFloatValue();
        return static_cast<double>(juce::Decibels::decibelsToGain(db, -40.0f));
    });
    threshold_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(0, static_cast<float>(value));
    };

    // Ratio: 0.0–0.95, displayed as "1/value : 1"
    ratio_.slider.setRange(0.0, 0.95, 0.01);
    ratio_.slider.setValueFormatter([](double value) -> juce::String {
        if (value < 0.01)
            return "INF : 1";
        float display = 1.0f / static_cast<float>(value);
        return juce::String(display, 1) + " : 1";
    });
    ratio_.slider.setValueParser([](const juce::String& text) -> double {
        auto t = text.trim().toLowerCase();
        if (t.contains("inf"))
            return 0.0;
        t = t.replace(": 1", "").replace(":1", "").trim();
        float display = t.getFloatValue();
        if (display <= 0.0f)
            return 0.0;
        return static_cast<double>(1.0f / display);
    });
    ratio_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(1, static_cast<float>(value));
    };

    // Attack: 0.3–200.0 ms
    attack_.slider.setRange(0.3, 200.0, 0.1);
    attack_.slider.setValueFormatter(
        [](double value) -> juce::String { return juce::String(value, 1) + " ms"; });
    attack_.slider.setValueParser([](const juce::String& text) -> double {
        return text.trim().replace("ms", "").trim().getDoubleValue();
    });
    attack_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(2, static_cast<float>(value));
    };

    // Release: 10.0–300.0 ms
    release_.slider.setRange(10.0, 300.0, 0.1);
    release_.slider.setValueFormatter(
        [](double value) -> juce::String { return juce::String(value, 1) + " ms"; });
    release_.slider.setValueParser([](const juce::String& text) -> double {
        return text.trim().replace("ms", "").trim().getDoubleValue();
    });
    release_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(3, static_cast<float>(value));
    };

    // Output Gain: -10.0–24.0 dB (uses Format::Decibels)
    output_.slider.setRange(-10.0, 24.0, 0.1);
    output_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(4, static_cast<float>(value));
    };

    // Sidechain Gain: -24.0–24.0 dB (uses Format::Decibels)
    sidechain_.slider.setRange(-24.0, 24.0, 0.1);
    sidechain_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(5, static_cast<float>(value));
    };

    // Sidechain Trigger: ON/OFF toggle button (virtual param index 6)
    setupLabelStatic(scTriggerLabel_, "SC TRIG", this);
    scTriggerButton_.setButtonText("OFF");
    scTriggerButton_.setClickingTogglesState(true);
    scTriggerButton_.setColour(juce::TextButton::buttonColourId,
                               DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.1f));
    scTriggerButton_.setColour(juce::TextButton::buttonOnColourId,
                               DarkTheme::getAccentColour().withAlpha(0.6f));
    scTriggerButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    scTriggerButton_.setColour(juce::TextButton::textColourOnId, DarkTheme::getTextColour());
    scTriggerButton_.onClick = [this]() {
        bool on = scTriggerButton_.getToggleState();
        scTriggerButton_.setButtonText(on ? "ON" : "OFF");
        if (onParameterChanged)
            onParameterChanged(6, on ? 1.0f : 0.0f);
    };
    addAndMakeVisible(scTriggerButton_);
}

void CompressorUI::setupSlider(SliderWithLabel& s, const juce::String& labelText) {
    setupLabelStatic(s.label, labelText, this);
    addAndMakeVisible(s.slider);
}

void CompressorUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    if (params.size() > 0)
        threshold_.slider.setValue(params[0].currentValue, juce::dontSendNotification);
    if (params.size() > 1)
        ratio_.slider.setValue(params[1].currentValue, juce::dontSendNotification);
    if (params.size() > 2)
        attack_.slider.setValue(params[2].currentValue, juce::dontSendNotification);
    if (params.size() > 3)
        release_.slider.setValue(params[3].currentValue, juce::dontSendNotification);
    if (params.size() > 4)
        output_.slider.setValue(params[4].currentValue, juce::dontSendNotification);
    if (params.size() > 5)
        sidechain_.slider.setValue(params[5].currentValue, juce::dontSendNotification);
    if (params.size() > 6) {
        bool on = params[6].currentValue >= 0.5f;
        scTriggerButton_.setToggleState(on, juce::dontSendNotification);
        scTriggerButton_.setButtonText(on ? "ON" : "OFF");
    }
}

void CompressorUI::paint(juce::Graphics& g) {
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(getLocalBounds().reduced(1));
}

void CompressorUI::resized() {
    auto area = getLocalBounds().reduced(6);
    int colWidth = area.getWidth() / 3;
    int rowHeight = (area.getHeight() - 8) / 3;  // 4px gap between rows
    int labelHeight = 14;
    int sliderHeight = 18;

    SliderWithLabel* row1[] = {&threshold_, &ratio_, &output_};
    SliderWithLabel* row2[] = {&attack_, &release_, &sidechain_};

    auto topRow = area.removeFromTop(rowHeight);
    for (auto* s : row1) {
        auto col = topRow.removeFromLeft(colWidth).reduced(2, 0);
        s->label.setBounds(col.removeFromTop(labelHeight));
        s->slider.setBounds(col.removeFromTop(sliderHeight));
    }

    area.removeFromTop(4);

    auto midRow = area.removeFromTop(rowHeight);
    for (auto* s : row2) {
        auto col = midRow.removeFromLeft(colWidth).reduced(2, 0);
        s->label.setBounds(col.removeFromTop(labelHeight));
        s->slider.setBounds(col.removeFromTop(sliderHeight));
    }

    area.removeFromTop(4);

    auto bottomRow = area.removeFromTop(rowHeight);
    auto col = bottomRow.removeFromLeft(colWidth).reduced(2, 0);
    scTriggerLabel_.setBounds(col.removeFromTop(labelHeight));
    scTriggerButton_.setBounds(col.removeFromTop(sliderHeight));
}

std::vector<LinkableTextSlider*> CompressorUI::getLinkableSliders() {
    return {&threshold_.slider, &ratio_.slider,  &attack_.slider,
            &release_.slider,   &output_.slider, &sidechain_.slider};
}

}  // namespace magda::daw::ui
