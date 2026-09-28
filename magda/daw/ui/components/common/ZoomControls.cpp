#include "ZoomControls.hpp"

#include "ActiveTheme.hpp"

namespace magda {

ZoomControls::ZoomControls() {
    // Setup buttons
    setupButton(zoomOutButton, "-");
    setupButton(zoomInButton, "+");

    // Setup slider
    setupSlider();

    // Add components
    addAndMakeVisible(zoomOutButton);
    addAndMakeVisible(zoomSlider);
    addAndMakeVisible(zoomInButton);

    // Button callbacks
    zoomOutButton.onClick = [this]() { handleZoomOut(); };
    zoomInButton.onClick = [this]() { handleZoomIn(); };

    // Slider callback
    zoomSlider.onValueChange = [this]() { handleSliderChange(); };
}

void ZoomControls::paint(juce::Graphics& g) {
    // Background
    g.setColour(ActiveTheme::getColour(ActiveTheme::BACKGROUND_ALT));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);

    // Subtle border
    g.setColour(ActiveTheme::getColour(ActiveTheme::BORDER).withAlpha(0.5f));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f, 1.0f);
}

void ZoomControls::resized() {
    auto bounds = getLocalBounds().reduced(4);

    const int buttonSize = bounds.getHeight();
    const int sliderMinWidth = 60;

    // Layout: [- button] [slider] [+ button]
    zoomOutButton.setBounds(bounds.removeFromLeft(buttonSize));
    bounds.removeFromLeft(4);  // spacing

    zoomInButton.setBounds(bounds.removeFromRight(buttonSize));
    bounds.removeFromRight(4);  // spacing

    // Slider takes remaining space
    if (bounds.getWidth() >= sliderMinWidth) {
        zoomSlider.setBounds(bounds);
        zoomSlider.setVisible(true);
    } else {
        zoomSlider.setVisible(false);
    }
}

void ZoomControls::setZoomLevel(double normalizedZoom) {
    // Clamp to valid range
    normalizedZoom = juce::jlimit(0.0, 1.0, normalizedZoom);
    zoomSlider.setValue(normalizedZoom, juce::dontSendNotification);
}

double ZoomControls::getZoomLevel() const {
    return zoomSlider.getValue();
}

void ZoomControls::setZoomRange(double min, double max) {
    minZoom = min;
    maxZoom = max;
}

void ZoomControls::handleZoomOut() {
    if (onZoomOut) {
        onZoomOut();
    } else if (onZoomChanged) {
        // Default: decrease by 20%
        double current = zoomSlider.getValue();
        double newValue = juce::jmax(0.0, current - 0.1);
        zoomSlider.setValue(newValue);
    }
}

void ZoomControls::handleZoomIn() {
    if (onZoomIn) {
        onZoomIn();
    } else if (onZoomChanged) {
        // Default: increase by 20%
        double current = zoomSlider.getValue();
        double newValue = juce::jmin(1.0, current + 0.1);
        zoomSlider.setValue(newValue);
    }
}

void ZoomControls::handleSliderChange() {
    if (onZoomChanged) {
        onZoomChanged(zoomSlider.getValue());
    }
}

void ZoomControls::setupButton(juce::TextButton& button, const juce::String& text) {
    button.setButtonText(text);
    button.setColour(juce::TextButton::buttonColourId,
                     ActiveTheme::getColour(ActiveTheme::BUTTON_NORMAL));
    button.setColour(juce::TextButton::buttonOnColourId,
                     ActiveTheme::getColour(ActiveTheme::BUTTON_ACTIVE));
    button.setColour(juce::TextButton::textColourOffId,
                     ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
    button.setColour(juce::TextButton::textColourOnId,
                     ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
}

void ZoomControls::setupSlider() {
    zoomSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    zoomSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    zoomSlider.setRange(0.0, 1.0, 0.001);  // Normalized range
    zoomSlider.setValue(0.5);              // Default to middle

    // Custom colors
    zoomSlider.setColour(juce::Slider::backgroundColourId,
                         ActiveTheme::getColour(ActiveTheme::SURFACE));
    zoomSlider.setColour(juce::Slider::trackColourId,
                         ActiveTheme::getColour(ActiveTheme::CONTROL_VALUE_FILL));
    zoomSlider.setColour(juce::Slider::thumbColourId,
                         ActiveTheme::getColour(ActiveTheme::CONTROL_SLIDER_THUMB));
}

}  // namespace magda
