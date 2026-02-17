#include "FilterUI.hpp"

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

FilterUI::FilterUI() {
    setupSlider(frequency_, "FREQ");

    // Frequency: 10–22000 Hz
    frequency_.slider.setRange(10.0, 22000.0, 1.0);
    frequency_.slider.setValueFormatter([](double value) -> juce::String {
        if (value >= 1000.0)
            return juce::String(value / 1000.0, 1) + " kHz";
        return juce::String(juce::roundToInt(value)) + " Hz";
    });
    frequency_.slider.setValueParser([](const juce::String& text) -> double {
        auto t = text.trim().toLowerCase();
        if (t.contains("khz"))
            return t.replace("khz", "").trim().getDoubleValue() * 1000.0;
        return t.replace("hz", "").trim().getDoubleValue();
    });
    frequency_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(0, static_cast<float>(value));
    };

    // Mode toggle button
    setupLabelStatic(modeLabel_, "MODE", this);
    modeButton_.setButtonText("LPF");
    modeButton_.setClickingTogglesState(true);
    modeButton_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.1f));
    modeButton_.setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getAccentColour().withAlpha(0.6f));
    modeButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    modeButton_.setColour(juce::TextButton::textColourOnId, DarkTheme::getTextColour());
    modeButton_.onClick = [this]() {
        bool isHighPass = modeButton_.getToggleState();
        modeButton_.setButtonText(isHighPass ? "HPF" : "LPF");
        if (onParameterChanged)
            onParameterChanged(1, isHighPass ? 1.0f : 0.0f);
    };
    addAndMakeVisible(modeButton_);
}

void FilterUI::setupSlider(SliderWithLabel& s, const juce::String& labelText) {
    setupLabelStatic(s.label, labelText, this);
    addAndMakeVisible(s.slider);
}

void FilterUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    if (params.size() > 0)
        frequency_.slider.setValue(params[0].currentValue, juce::dontSendNotification);
    if (params.size() > 1) {
        bool isHighPass = params[1].currentValue >= 0.5f;
        modeButton_.setToggleState(isHighPass, juce::dontSendNotification);
        modeButton_.setButtonText(isHighPass ? "HPF" : "LPF");
    }
}

void FilterUI::paint(juce::Graphics& g) {
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(getLocalBounds().reduced(1));
}

void FilterUI::resized() {
    auto area = getLocalBounds().reduced(6);
    int colWidth = area.getWidth() / 2;
    int labelHeight = 14;
    int sliderHeight = 18;

    int totalHeight = labelHeight + sliderHeight;
    auto row = area.withSizeKeepingCentre(area.getWidth(), totalHeight);

    // Frequency slider column
    auto freqCol = row.removeFromLeft(colWidth).reduced(2, 0);
    frequency_.label.setBounds(freqCol.removeFromTop(labelHeight));
    frequency_.slider.setBounds(freqCol.removeFromTop(sliderHeight));

    // Mode toggle column
    auto modeCol = row.removeFromLeft(colWidth).reduced(2, 0);
    modeLabel_.setBounds(modeCol.removeFromTop(labelHeight));
    modeButton_.setBounds(modeCol.removeFromTop(sliderHeight));
}

std::vector<LinkableTextSlider*> FilterUI::getLinkableSliders() {
    return {&frequency_.slider};
}

}  // namespace magda::daw::ui
