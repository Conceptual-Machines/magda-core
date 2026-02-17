#include "ReverbUI.hpp"

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

ReverbUI::ReverbUI() {
    setupSlider(roomSize_, "ROOM");
    setupSlider(damping_, "DAMP");
    setupSlider(width_, "WIDTH");
    setupSlider(wetLevel_, "WET");
    setupSlider(dryLevel_, "DRY");
    setupSlider(freeze_, "FREEZE");

    // Room Size: 0.0–1.0, displayed as 1–11
    roomSize_.slider.setRange(0.0, 1.0, 0.01);
    roomSize_.slider.setValueFormatter([](double value) -> juce::String {
        return juce::String(1 + static_cast<int>(10.0 * value));
    });
    roomSize_.slider.setValueParser([](const juce::String& text) -> double {
        int display = text.trim().getIntValue();
        return juce::jlimit(0.0, 1.0, static_cast<double>(display - 1) / 10.0);
    });
    roomSize_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(0, static_cast<float>(value));
    };

    // Damping: 0.0–1.0, displayed as percentage
    damping_.slider.setRange(0.0, 1.0, 0.01);
    damping_.slider.setValueFormatter([](double value) -> juce::String {
        return juce::String(static_cast<int>(100.0 * value)) + "%";
    });
    damping_.slider.setValueParser([](const juce::String& text) -> double {
        return text.trim().replace("%", "").trim().getDoubleValue() / 100.0;
    });
    damping_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(1, static_cast<float>(value));
    };

    // Wet Level: 0.0–1.0, displayed as dB (gainToDb(3.0 * value))
    wetLevel_.slider.setRange(0.0, 1.0, 0.01);
    wetLevel_.slider.setValueFormatter([](double value) -> juce::String {
        float gain = 3.0f * static_cast<float>(value);
        if (gain <= 0.0f)
            return "-inf dB";
        float db = juce::Decibels::gainToDecibels(gain);
        return juce::String(db, 1) + " dB";
    });
    wetLevel_.slider.setValueParser([](const juce::String& text) -> double {
        auto t = text.trim().toLowerCase();
        if (t.contains("inf"))
            return 0.0;
        t = t.replace("db", "").trim();
        float db = t.getFloatValue();
        float gain = juce::Decibels::decibelsToGain(db);
        return juce::jlimit(0.0, 1.0, static_cast<double>(gain / 3.0f));
    });
    wetLevel_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(2, static_cast<float>(value));
    };

    // Dry Level: 0.0–1.0, displayed as dB (gainToDb(2.0 * value))
    dryLevel_.slider.setRange(0.0, 1.0, 0.01);
    dryLevel_.slider.setValueFormatter([](double value) -> juce::String {
        float gain = 2.0f * static_cast<float>(value);
        if (gain <= 0.0f)
            return "-inf dB";
        float db = juce::Decibels::gainToDecibels(gain);
        return juce::String(db, 1) + " dB";
    });
    dryLevel_.slider.setValueParser([](const juce::String& text) -> double {
        auto t = text.trim().toLowerCase();
        if (t.contains("inf"))
            return 0.0;
        t = t.replace("db", "").trim();
        float db = t.getFloatValue();
        float gain = juce::Decibels::decibelsToGain(db);
        return juce::jlimit(0.0, 1.0, static_cast<double>(gain / 2.0f));
    });
    dryLevel_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(3, static_cast<float>(value));
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
            onParameterChanged(4, static_cast<float>(value));
    };

    // Freeze: 0 or 1, displayed as ON/OFF
    freeze_.slider.setRange(0.0, 1.0, 1.0);
    freeze_.slider.setValueFormatter(
        [](double value) -> juce::String { return value >= 0.5 ? "ON" : "OFF"; });
    freeze_.slider.setValueParser([](const juce::String& text) -> double {
        return text.trim().toLowerCase() == "on" ? 1.0 : 0.0;
    });
    freeze_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(5, static_cast<float>(value));
    };
}

void ReverbUI::setupSlider(SliderWithLabel& s, const juce::String& labelText) {
    setupLabelStatic(s.label, labelText, this);
    addAndMakeVisible(s.slider);
}

void ReverbUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    if (params.size() > 0)
        roomSize_.slider.setValue(params[0].currentValue, juce::dontSendNotification);
    if (params.size() > 1)
        damping_.slider.setValue(params[1].currentValue, juce::dontSendNotification);
    if (params.size() > 2)
        wetLevel_.slider.setValue(params[2].currentValue, juce::dontSendNotification);
    if (params.size() > 3)
        dryLevel_.slider.setValue(params[3].currentValue, juce::dontSendNotification);
    if (params.size() > 4)
        width_.slider.setValue(params[4].currentValue, juce::dontSendNotification);
    if (params.size() > 5)
        freeze_.slider.setValue(params[5].currentValue, juce::dontSendNotification);
}

void ReverbUI::paint(juce::Graphics& g) {
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(getLocalBounds().reduced(1));
}

void ReverbUI::resized() {
    auto area = getLocalBounds().reduced(6);
    int colWidth = area.getWidth() / 3;
    int rowHeight = (area.getHeight() - 4) / 2;  // 4px gap between rows
    int labelHeight = 14;
    int sliderHeight = 18;

    SliderWithLabel* row1[] = {&roomSize_, &damping_, &width_};
    SliderWithLabel* row2[] = {&wetLevel_, &dryLevel_, &freeze_};

    auto topRow = area.removeFromTop(rowHeight);
    for (auto* s : row1) {
        auto col = topRow.removeFromLeft(colWidth).reduced(2, 0);
        s->label.setBounds(col.removeFromTop(labelHeight));
        s->slider.setBounds(col.removeFromTop(sliderHeight));
    }

    area.removeFromTop(4);

    auto bottomRow = area.removeFromTop(rowHeight);
    for (auto* s : row2) {
        auto col = bottomRow.removeFromLeft(colWidth).reduced(2, 0);
        s->label.setBounds(col.removeFromTop(labelHeight));
        s->slider.setBounds(col.removeFromTop(sliderHeight));
    }
}

std::vector<LinkableTextSlider*> ReverbUI::getLinkableSliders() {
    return {&roomSize_.slider, &damping_.slider, &wetLevel_.slider,
            &dryLevel_.slider, &width_.slider,   &freeze_.slider};
}

}  // namespace magda::daw::ui
