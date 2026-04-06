#include "PadChainRowComponent.hpp"

#include <BinaryData.h>

#include "audio/DrumGridPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

PadChainRowComponent::PadChainRowComponent(int padIndex) : padIndex_(padIndex) {
    // Name label - clicks pass through to parent for selection
    nameLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    nameLabel_.setJustificationType(juce::Justification::centredLeft);
    nameLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(nameLabel_);

    // Level text slider (dB format)
    levelSlider_.setRange(-60.0, 12.0, 0.1);
    levelSlider_.setValue(0.0, juce::dontSendNotification);
    levelSlider_.onValueChanged = [this](double value) {
        if (onLevelChanged)
            onLevelChanged(padIndex_, static_cast<float>(value));
    };
    addAndMakeVisible(levelSlider_);

    // Pan text slider (L/C/R format)
    panSlider_.setRange(-1.0, 1.0, 0.01);
    panSlider_.setValue(0.0, juce::dontSendNotification);
    panSlider_.onValueChanged = [this](double value) {
        if (onPanChanged)
            onPanChanged(padIndex_, static_cast<float>(value));
    };
    addAndMakeVisible(panSlider_);

    // Output bus selector button
    outputButton_.setColour(juce::TextButton::buttonColourId,
                            DarkTheme::getColour(DarkTheme::SURFACE).brighter(0.05f));
    outputButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    outputButton_.onClick = [this]() {
        juce::PopupMenu menu;
        menu.addItem(1, "Main", true, currentBusOutput_ == 0);
        for (int bus = 1; bus < daw::audio::DrumGridPlugin::maxBusOutputs; ++bus)
            menu.addItem(bus + 1, "Bus " + juce::String(bus), true, currentBusOutput_ == bus);
        juce::Component::SafePointer<PadChainRowComponent> safeThis(this);
        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(&outputButton_), [safeThis](int result) {
                if (result <= 0 || safeThis == nullptr)
                    return;
                int busIndex = result - 1;  // item 1 = Main (0), item 2 = Bus 1, etc.
                safeThis->currentBusOutput_ = busIndex;
                safeThis->outputButton_.setButtonText(busIndex == 0 ? "Main"
                                                                    : "B" + juce::String(busIndex));
                if (safeThis->onOutputChanged)
                    safeThis->onOutputChanged(safeThis->padIndex_, busIndex);
            });
    };
    outputButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addAndMakeVisible(outputButton_);

    // Mute button
    muteButton_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    muteButton_.setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::STATUS_WARNING));
    muteButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    muteButton_.setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
    muteButton_.setClickingTogglesState(true);
    muteButton_.onClick = [this]() {
        if (onMuteChanged)
            onMuteChanged(padIndex_, muteButton_.getToggleState());
    };
    muteButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addAndMakeVisible(muteButton_);

    // Solo button
    soloButton_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    soloButton_.setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    soloButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    soloButton_.setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
    soloButton_.setClickingTogglesState(true);
    soloButton_.onClick = [this]() {
        if (onSoloChanged)
            onSoloChanged(padIndex_, soloButton_.getToggleState());
    };
    soloButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addAndMakeVisible(soloButton_);

    // On/bypass button (power icon)
    onButton_ = std::make_unique<magda::SvgButton>("Power", BinaryData::power_on_svg,
                                                   BinaryData::power_on_svgSize);
    onButton_->setClickingTogglesState(true);
    onButton_->setToggleState(true, juce::dontSendNotification);
    onButton_->setNormalColor(DarkTheme::getColour(DarkTheme::STATUS_ERROR));
    onButton_->setActiveColor(juce::Colours::white);
    onButton_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_GREEN).darker(0.3f));
    onButton_->setActive(true);
    onButton_->onClick = [this]() {
        bool active = onButton_->getToggleState();
        onButton_->setActive(active);
        float alpha = active ? 1.0f : 0.35f;
        nameLabel_.setAlpha(alpha);
        muteButton_.setAlpha(alpha);
        soloButton_.setAlpha(alpha);
        levelSlider_.setAlpha(alpha);
        panSlider_.setAlpha(alpha);
        if (onBypassChanged)
            onBypassChanged(padIndex_, !active);
    };
    addAndMakeVisible(*onButton_);

    // Delete button (reddish-purple background)
    deleteButton_.setButtonText(juce::String::fromUTF8("\xc3\x97"));  // x symbol
    deleteButton_.setColour(
        juce::TextButton::buttonColourId,
        DarkTheme::getColour(DarkTheme::ACCENT_PURPLE)
            .interpolatedWith(DarkTheme::getColour(DarkTheme::STATUS_ERROR), 0.5f)
            .darker(0.2f));
    deleteButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    deleteButton_.onClick = [this]() {
        if (onDeleteClicked)
            onDeleteClicked(padIndex_);
    };
    deleteButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addAndMakeVisible(deleteButton_);
}

PadChainRowComponent::~PadChainRowComponent() {
    outputButton_.setLookAndFeel(nullptr);
    muteButton_.setLookAndFeel(nullptr);
    soloButton_.setLookAndFeel(nullptr);
    deleteButton_.setLookAndFeel(nullptr);
}

void PadChainRowComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // Background - highlight if selected
    if (selected_) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.2f));
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
    }
    g.fillRoundedRectangle(bounds.toFloat(), 2.0f);

    // Border - accent color if selected
    if (selected_) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    }
    g.drawRoundedRectangle(bounds.toFloat(), 2.0f, 1.0f);
}

void PadChainRowComponent::resized() {
    auto bounds = getLocalBounds().reduced(3, 2);

    // Right side buttons (from right to left)
    deleteButton_.setBounds(bounds.removeFromRight(16));
    bounds.removeFromRight(2);

    onButton_->setBounds(bounds.removeFromRight(16));
    bounds.removeFromRight(2);

    soloButton_.setBounds(bounds.removeFromRight(16));
    bounds.removeFromRight(2);

    muteButton_.setBounds(bounds.removeFromRight(16));
    bounds.removeFromRight(4);

    outputButton_.setBounds(bounds.removeFromRight(42));
    bounds.removeFromRight(4);

    // Left side elements
    nameLabel_.setBounds(bounds.removeFromLeft(50));
    bounds.removeFromLeft(4);

    // Pan gets a fixed small width; level takes the rest
    constexpr int PAN_WIDTH = 32;
    panSlider_.setBounds(bounds.removeFromRight(PAN_WIDTH));
    bounds.removeFromRight(4);

    levelSlider_.setBounds(bounds);
}

void PadChainRowComponent::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        if (onRightClicked)
            onRightClicked(padIndex_, e.getScreenPosition());
    }
}

void PadChainRowComponent::mouseUp(const juce::MouseEvent& e) {
    if (!contains(e.getPosition()))
        return;

    if (onClicked)
        onClicked(padIndex_);
}

void PadChainRowComponent::updateFromPad(const juce::String& name, float level, float pan,
                                         bool mute, bool solo, bool bypassed, int busOutput) {
    nameLabel_.setText(name, juce::dontSendNotification);
    levelSlider_.setValue(level, juce::dontSendNotification);
    panSlider_.setValue(pan, juce::dontSendNotification);
    muteButton_.setToggleState(mute, juce::dontSendNotification);
    soloButton_.setToggleState(solo, juce::dontSendNotification);
    onButton_->setToggleState(!bypassed, juce::dontSendNotification);
    onButton_->setActive(!bypassed);

    currentBusOutput_ = busOutput;
    outputButton_.setButtonText(busOutput == 0 ? "Main" : "B" + juce::String(busOutput));

    // Dim controls when bypassed
    float alpha = bypassed ? 0.35f : 1.0f;
    nameLabel_.setAlpha(alpha);
    muteButton_.setAlpha(alpha);
    soloButton_.setAlpha(alpha);
    levelSlider_.setAlpha(alpha);
    panSlider_.setAlpha(alpha);
    outputButton_.setAlpha(alpha);
}

void PadChainRowComponent::setSelected(bool selected) {
    if (selected_ != selected) {
        selected_ = selected;
        repaint();
    }
}

}  // namespace magda::daw::ui
