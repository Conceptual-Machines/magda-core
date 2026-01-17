#include "LayoutDebugPanel.hpp"

#include "../../themes/DarkTheme.hpp"

namespace magica {

LayoutDebugPanel::LayoutDebugPanel() {
    auto& layout = LayoutConfig::getInstance();

    // Timeline heights
    addSlider("Arrangement Bar", &layout.arrangementBarHeight, 10, 80);
    addSlider("Time Ruler", &layout.timeRulerHeight, 20, 100);

    // Ruler details
    addSlider("Major Tick", &layout.rulerMajorTickHeight, 4, 30);
    addSlider("Minor Tick", &layout.rulerMinorTickHeight, 2, 20);
    addSlider("Label Font", &layout.rulerLabelFontSize, 8, 16);
    addSlider("Label Margin", &layout.rulerLabelTopMargin, 0, 20);

    // Track
    addSlider("Track Height", &layout.defaultTrackHeight, 40, 200);
    addSlider("Header Width", &layout.defaultTrackHeaderWidth, 100, 400);

    setSize(220, static_cast<int>(rows.size()) * 50 + 30);
}

void LayoutDebugPanel::paint(juce::Graphics& g) {
    // Semi-transparent dark background
    g.setColour(juce::Colour(0xE0101015));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1), 8.0f, 2.0f);

    // Title
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    g.setFont(13.0f);
    g.drawText("Layout Debug (F11)", 10, 5, getWidth() - 20, 20, juce::Justification::centred);
}

void LayoutDebugPanel::resized() {
    int y = 28;
    const int rowHeight = 50;
    const int labelHeight = 16;
    const int sliderHeight = 24;
    const int margin = 10;

    for (auto& row : rows) {
        row.label->setBounds(margin, y, getWidth() - margin * 2, labelHeight);
        row.slider->setBounds(margin, y + labelHeight + 2, getWidth() - margin * 2, sliderHeight);
        y += rowHeight;
    }
}

void LayoutDebugPanel::addSlider(const juce::String& name, int* valuePtr, int min, int max) {
    SliderRow row;
    row.valuePtr = valuePtr;

    row.label = std::make_unique<juce::Label>();
    row.label->setText(name + ": " + juce::String(*valuePtr), juce::dontSendNotification);
    row.label->setColour(juce::Label::textColourId,
                         DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    row.label->setFont(11.0f);
    addAndMakeVisible(*row.label);

    row.slider =
        std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    row.slider->setRange(min, max, 1);
    row.slider->setValue(*valuePtr, juce::dontSendNotification);
    row.slider->setColour(juce::Slider::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    row.slider->setColour(juce::Slider::trackColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    row.slider->setColour(juce::Slider::thumbColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_BLUE).brighter());

    // Capture valuePtr and label by pointer for the lambda
    auto* labelPtr = row.label.get();
    auto* slider = row.slider.get();

    row.slider->onValueChange = [this, valuePtr, labelPtr, slider, name]() {
        *valuePtr = static_cast<int>(slider->getValue());
        labelPtr->setText(name + ": " + juce::String(*valuePtr), juce::dontSendNotification);
        if (onLayoutChanged) {
            onLayoutChanged();
        }
    };

    addAndMakeVisible(*row.slider);
    rows.push_back(std::move(row));
}

void LayoutDebugPanel::updateFromConfig() {
    for (auto& row : rows) {
        row.slider->setValue(*row.valuePtr, juce::dontSendNotification);
    }
}

}  // namespace magica
