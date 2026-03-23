#include "UtilityUI.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include "BinaryData.h"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace te = tracktion;

static void setupLabelStatic(juce::Label& label, const juce::String& text,
                             juce::Component* parent) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(FontManager::getInstance().getUIFont(9.0f));
    label.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    label.setJustificationType(juce::Justification::centred);
    parent->addAndMakeVisible(label);
}

UtilityUI::UtilityUI() {
    setupSlider(gain_, "GAIN");
    setupSlider(pan_, "PAN");

    // Gain: slider position 0..1, displayed as dB via volumeFaderPositionToDB
    gain_.slider.setRange(0.0, 1.0, 0.001);
    gain_.slider.setValueFormatter([](double value) -> juce::String {
        float db = te::volumeFaderPositionToDB(static_cast<float>(value));
        if (db <= -100.0f)
            return "-inf dB";
        return juce::String(db, 1) + " dB";
    });
    gain_.slider.setValueParser([](const juce::String& text) -> double {
        auto t = text.trim().toLowerCase();
        if (t.contains("inf"))
            return 0.0;
        t = t.replace("db", "").trim();
        float db = t.getFloatValue();
        return static_cast<double>(te::decibelsToVolumeFaderPosition(db));
    });
    gain_.slider.setValue(te::decibelsToVolumeFaderPosition(0.0f), juce::dontSendNotification);
    gain_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(0, static_cast<float>(value));
    };

    // Pan: -1..1, displayed as L/C/R
    pan_.slider.setRange(-1.0, 1.0, 0.01);
    pan_.slider.setValueFormatter([](double value) -> juce::String {
        if (std::abs(value) < 0.01)
            return "C";
        if (value < 0.0)
            return juce::String(static_cast<int>(-value * 100)) + "L";
        return juce::String(static_cast<int>(value * 100)) + "R";
    });
    pan_.slider.setValueParser([](const juce::String& text) -> double {
        auto t = text.trim().toUpperCase();
        if (t == "C" || t == "0")
            return 0.0;
        if (t.endsWithChar('L'))
            return -t.dropLastCharacters(1).getDoubleValue() / 100.0;
        if (t.endsWithChar('R'))
            return t.dropLastCharacters(1).getDoubleValue() / 100.0;
        return t.getDoubleValue();
    });
    pan_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(1, static_cast<float>(value));
    };

    // Phase Invert: toggle button with polarity icon (param index 2)
    setupLabelStatic(phaseLabel_, "PHASE", this);
    phaseButton_ = std::make_unique<magda::SvgButton>("Phase", BinaryData::phase_invert_svg,
                                                      BinaryData::phase_invert_svgSize);
    phaseButton_->setClickingTogglesState(true);
    phaseButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    phaseButton_->setActiveColor(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    phaseButton_->setActiveBackgroundColor(
        DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.35f));
    phaseButton_->onClick = [this]() {
        bool on = phaseButton_->getToggleState();
        phaseButton_->setActive(on);
        if (onParameterChanged)
            onParameterChanged(2, on ? 1.0f : 0.0f);
    };
    addAndMakeVisible(*phaseButton_);
}

void UtilityUI::setupSlider(SliderWithLabel& s, const juce::String& labelText) {
    setupLabelStatic(s.label, labelText, this);
    addAndMakeVisible(s.slider);
}

void UtilityUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    if (params.size() > 0)
        gain_.slider.setValue(params[0].currentValue, juce::dontSendNotification);
    if (params.size() > 1)
        pan_.slider.setValue(params[1].currentValue, juce::dontSendNotification);
    if (params.size() > 2) {
        bool on = params[2].currentValue >= 0.5f;
        phaseButton_->setToggleState(on, juce::dontSendNotification);
        phaseButton_->setActive(on);
    }
}

void UtilityUI::paint(juce::Graphics& g) {
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(getLocalBounds().reduced(1));
}

void UtilityUI::resized() {
    auto area = getLocalBounds().reduced(6);
    int labelHeight = 14;
    int sliderHeight = 18;
    int totalWidth = area.getWidth();
    int gainColWidth = totalWidth * 50 / 100;
    int panColWidth = totalWidth * 35 / 100;
    int phaseColWidth = totalWidth - gainColWidth - panColWidth;

    auto row = area;

    // Gain slider
    auto gainCol = row.removeFromLeft(gainColWidth).reduced(2, 0);
    gain_.label.setBounds(gainCol.removeFromTop(labelHeight));
    gain_.slider.setBounds(gainCol.removeFromTop(sliderHeight));

    // Pan slider
    auto panCol = row.removeFromLeft(panColWidth).reduced(2, 0);
    pan_.label.setBounds(panCol.removeFromTop(labelHeight));
    pan_.slider.setBounds(panCol.removeFromTop(sliderHeight));

    // Phase button — compact column
    auto phaseCol = row.reduced(2, 0);
    phaseLabel_.setBounds(phaseCol.removeFromTop(labelHeight));
    auto phaseArea = phaseCol.removeFromTop(sliderHeight);
    int phaseSize = juce::jmin(phaseArea.getWidth(), phaseArea.getHeight());
    phaseButton_->setBounds(phaseArea.withSizeKeepingCentre(phaseSize, phaseSize));
}

std::vector<LinkableTextSlider*> UtilityUI::getLinkableSliders() {
    return {&gain_.slider, &pan_.slider};
}

}  // namespace magda::daw::ui
