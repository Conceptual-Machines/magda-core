#include "ImpulseResponseUI.hpp"

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

ImpulseResponseUI::ImpulseResponseUI() {
    // IR name label
    irNameLabel_.setText("No IR loaded", juce::dontSendNotification);
    irNameLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    irNameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    irNameLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(irNameLabel_);

    // Load button
    loadButton_.setButtonText("LOAD");
    loadButton_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getAccentColour().withAlpha(0.5f));
    loadButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    loadButton_.onClick = [this]() {
        if (onLoadIRRequested)
            onLoadIRRequested();
    };
    addAndMakeVisible(loadButton_);

    setupSlider(gain_, "GAIN");
    setupSlider(lowCut_, "LOW CUT");
    setupSlider(highCut_, "HIGH CUT");
    setupSlider(mix_, "MIX");
    setupSlider(filterQ_, "Q");

    // Gain: -12 to +6 dB (uses Decibels format, default formatter is fine)
    gain_.slider.setRange(-12.0, 6.0, 0.1);
    gain_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(0, static_cast<float>(value));
    };

    // Low Cut (high-pass, log-scaled drag)
    lowCut_.slider.setRange(10.0, 20000.0, 1.0);
    lowCut_.slider.setSkewForCentre(1000.0);
    lowCut_.slider.setValueFormatter([](double value) -> juce::String {
        if (value >= 1000.0)
            return juce::String(value / 1000.0, 1) + " kHz";
        return juce::String(juce::roundToInt(value)) + " Hz";
    });
    lowCut_.slider.setValueParser([](const juce::String& text) -> double {
        auto t = text.trim().toLowerCase();
        if (t.contains("khz"))
            return t.replace("khz", "").trim().getDoubleValue() * 1000.0;
        return t.replace("hz", "").trim().getDoubleValue();
    });
    lowCut_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(1, static_cast<float>(value));
    };

    // High Cut (low-pass, log-scaled drag)
    highCut_.slider.setRange(10.0, 20000.0, 1.0);
    highCut_.slider.setSkewForCentre(1000.0);
    highCut_.slider.setValueFormatter([](double value) -> juce::String {
        if (value >= 1000.0)
            return juce::String(value / 1000.0, 1) + " kHz";
        return juce::String(juce::roundToInt(value)) + " Hz";
    });
    highCut_.slider.setValueParser([](const juce::String& text) -> double {
        auto t = text.trim().toLowerCase();
        if (t.contains("khz"))
            return t.replace("khz", "").trim().getDoubleValue() * 1000.0;
        return t.replace("hz", "").trim().getDoubleValue();
    });
    highCut_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(2, static_cast<float>(value));
    };

    // Mix: 0–100%
    mix_.slider.setRange(0.0, 1.0, 0.01);
    mix_.slider.setValueFormatter([](double value) -> juce::String {
        return juce::String(juce::roundToInt(value * 100.0)) + "%";
    });
    mix_.slider.setValueParser([](const juce::String& text) -> double {
        return text.trim().replace("%", "").trim().getDoubleValue() / 100.0;
    });
    mix_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(3, static_cast<float>(value));
    };

    // Filter Q: 0.1–14.0
    filterQ_.slider.setRange(0.1, 14.0, 0.01);
    filterQ_.slider.setValueFormatter(
        [](double value) -> juce::String { return juce::String(value, 2); });
    filterQ_.slider.setValueParser(
        [](const juce::String& text) -> double { return text.trim().getDoubleValue(); });
    filterQ_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(4, static_cast<float>(value));
    };
}

void ImpulseResponseUI::setupSlider(SliderWithLabel& s, const juce::String& labelText) {
    setupLabelStatic(s.label, labelText, this);
    addAndMakeVisible(s.slider);
}

void ImpulseResponseUI::setIRName(const juce::String& name) {
    irNameLabel_.setText(name.isEmpty() ? "No IR loaded" : name, juce::dontSendNotification);
}

void ImpulseResponseUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    if (params.size() > 0)
        gain_.slider.setValue(params[0].currentValue, juce::dontSendNotification);
    if (params.size() > 1)
        lowCut_.slider.setValue(params[1].currentValue, juce::dontSendNotification);
    if (params.size() > 2)
        highCut_.slider.setValue(params[2].currentValue, juce::dontSendNotification);
    if (params.size() > 3)
        mix_.slider.setValue(params[3].currentValue, juce::dontSendNotification);
    if (params.size() > 4)
        filterQ_.slider.setValue(params[4].currentValue, juce::dontSendNotification);
}

void ImpulseResponseUI::paint(juce::Graphics& g) {
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(getLocalBounds().reduced(1));
}

void ImpulseResponseUI::resized() {
    auto area = getLocalBounds().reduced(6);
    int colWidth = area.getWidth() / 3;
    int labelHeight = 14;
    int sliderHeight = 18;

    // Top row: IR name + load button
    auto fileRow = area.removeFromTop(20);
    loadButton_.setBounds(fileRow.removeFromRight(50));
    fileRow.removeFromRight(4);
    irNameLabel_.setBounds(fileRow);

    area.removeFromTop(4);

    int rowHeight = (area.getHeight() - 4) / 2;

    // Row 1: Low Cut, High Cut, Q
    auto topRow = area.removeFromTop(rowHeight);
    SliderWithLabel* row1[] = {&lowCut_, &highCut_, &filterQ_};
    for (auto* s : row1) {
        auto col = topRow.removeFromLeft(colWidth).reduced(2, 0);
        s->label.setBounds(col.removeFromTop(labelHeight));
        s->slider.setBounds(col.removeFromTop(sliderHeight));
    }

    area.removeFromTop(4);

    // Row 2: Gain, Mix
    auto bottomRow = area.removeFromTop(rowHeight);
    SliderWithLabel* row2[] = {&gain_, &mix_};
    for (auto* s : row2) {
        auto col = bottomRow.removeFromLeft(colWidth).reduced(2, 0);
        s->label.setBounds(col.removeFromTop(labelHeight));
        s->slider.setBounds(col.removeFromTop(sliderHeight));
    }
}

bool ImpulseResponseUI::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& f : files) {
        juce::File file(f);
        auto ext = file.getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac" || ext == ".ogg")
            return true;
    }
    return false;
}

void ImpulseResponseUI::filesDropped(const juce::StringArray& files, int /*x*/, int /*y*/) {
    for (const auto& f : files) {
        juce::File file(f);
        if (file.existsAsFile()) {
            if (onFileDropped)
                onFileDropped(file);
            return;
        }
    }
}

std::vector<LinkableTextSlider*> ImpulseResponseUI::getLinkableSliders() {
    return {&gain_.slider, &lowCut_.slider, &highCut_.slider, &mix_.slider, &filterQ_.slider};
}

}  // namespace magda::daw::ui
