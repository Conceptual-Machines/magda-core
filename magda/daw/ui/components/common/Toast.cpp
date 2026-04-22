#include "Toast.hpp"

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

// static
Toast* Toast::globalHost_ = nullptr;

// ============================================================================
// Construction
// ============================================================================

Toast::Toast() {
    label_.setFont(FontManager::getInstance().getUIFont(13.0f));
    label_.setColour(juce::Label::textColourId, juce::Colours::white);
    label_.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label_);

    setInterceptsMouseClicks(false, false);
    setVisible(false);
}

Toast::~Toast() {
    if (globalHost_ == this)
        globalHost_ = nullptr;
}

// ============================================================================
// Show / hide
// ============================================================================

void Toast::show(const juce::String& text, int durationMs) {
    currentText_ = text;
    label_.setText(text, juce::dontSendNotification);

    // Resize to fit text + padding
    const int hPad = 16;
    const int vPad = 8;
    const int h = 32;
    auto font = label_.getFont();
    int textW = static_cast<int>(font.getStringWidthFloat(text)) + hPad * 2;
    textW = juce::jmax(120, textW);
    setSize(textW, h + vPad * 2);

    setVisible(true);
    toFront(false);
    repaint();

    stopTimer();
    if (durationMs > 0)
        startTimer(durationMs);
}

void Toast::timerCallback() {
    stopTimer();
    setVisible(false);
}

// ============================================================================
// Static global host
// ============================================================================

void Toast::setGlobalHost(Toast* host) {
    globalHost_ = host;
}

void Toast::showGlobal(const juce::String& text, int durationMs) {
    if (globalHost_ == nullptr)
        return;
    globalHost_->show(text, durationMs);
}

// ============================================================================
// Painting
// ============================================================================

void Toast::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    auto bgColour = juce::Colour(0xFF222233).withAlpha(0.92f);
    g.setColour(bgColour);
    g.fillRoundedRectangle(bounds, 6.0f);

    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE_LIGHT).withAlpha(0.6f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);
}

void Toast::resized() {
    label_.setBounds(getLocalBounds());
}

}  // namespace magda::daw::ui
