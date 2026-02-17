#include "PitchShiftUI.hpp"

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

PitchShiftUI::PitchShiftUI() {
    setupSlider(semitones_, "SEMITONES");

    // Semitones: -24 to +24
    semitones_.slider.setRange(-24.0, 24.0, 0.1);
    semitones_.slider.setValueFormatter([](double value) -> juce::String {
        if (std::abs(value) < 0.01)
            return "0 st";
        return juce::String(value, 1) + " st";
    });
    semitones_.slider.setValueParser([](const juce::String& text) -> double {
        return text.trim().replace("st", "").trim().getDoubleValue();
    });
    semitones_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(0, static_cast<float>(value));
    };
}

void PitchShiftUI::setupSlider(SliderWithLabel& s, const juce::String& labelText) {
    setupLabelStatic(s.label, labelText, this);
    addAndMakeVisible(s.slider);
}

void PitchShiftUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    if (params.size() > 0)
        semitones_.slider.setValue(params[0].currentValue, juce::dontSendNotification);
}

void PitchShiftUI::paint(juce::Graphics& g) {
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(getLocalBounds().reduced(1));
}

void PitchShiftUI::resized() {
    auto area = getLocalBounds().reduced(6);
    int labelHeight = 14;
    int sliderHeight = 18;

    int totalHeight = labelHeight + sliderHeight;
    auto row = area.withSizeKeepingCentre(area.getWidth(), totalHeight);

    auto col = row.reduced(2, 0);
    semitones_.label.setBounds(col.removeFromTop(labelHeight));
    semitones_.slider.setBounds(col.removeFromTop(sliderHeight));
}

std::vector<LinkableTextSlider*> PitchShiftUI::getLinkableSliders() {
    return {&semitones_.slider};
}

}  // namespace magda::daw::ui
