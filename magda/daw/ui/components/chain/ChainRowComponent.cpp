#include "ChainRowComponent.hpp"

#include "RackComponent.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

ChainRowComponent::ChainRowComponent(RackComponent& owner, magda::TrackId trackId,
                                     magda::RackId rackId, const magda::ChainInfo& chain)
    : owner_(owner), trackId_(trackId), rackId_(rackId), chainId_(chain.id) {
    // Name label - clicks pass through to parent for selection
    nameLabel_.setText(chain.name, juce::dontSendNotification);
    nameLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    nameLabel_.setJustificationType(juce::Justification::centredLeft);
    nameLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(nameLabel_);

    // Gain slider
    gainSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    gainSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    gainSlider_.setRange(0.0, 1.0, 0.01);
    gainSlider_.setValue(chain.volume);
    gainSlider_.setColour(juce::Slider::trackColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    gainSlider_.setColour(juce::Slider::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    addAndMakeVisible(gainSlider_);

    // Pan slider
    panSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    panSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    panSlider_.setRange(-1.0, 1.0, 0.01);
    panSlider_.setValue(chain.pan);
    panSlider_.setColour(juce::Slider::trackColourId, DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    panSlider_.setColour(juce::Slider::backgroundColourId,
                         DarkTheme::getColour(DarkTheme::SURFACE));
    addAndMakeVisible(panSlider_);

    // Mute button
    muteButton_.setButtonText("M");
    muteButton_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    muteButton_.setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::STATUS_WARNING));
    muteButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    muteButton_.setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
    muteButton_.setClickingTogglesState(true);
    muteButton_.setToggleState(chain.muted, juce::dontSendNotification);
    muteButton_.onClick = [this]() { onMuteClicked(); };
    muteButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addAndMakeVisible(muteButton_);

    // Solo button
    soloButton_.setButtonText("S");
    soloButton_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    soloButton_.setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    soloButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    soloButton_.setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
    soloButton_.setClickingTogglesState(true);
    soloButton_.setToggleState(chain.solo, juce::dontSendNotification);
    soloButton_.onClick = [this]() { onSoloClicked(); };
    soloButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addAndMakeVisible(soloButton_);

    // On/bypass button
    onButton_.setButtonText("On");
    onButton_.setColour(juce::TextButton::buttonColourId, DarkTheme::getColour(DarkTheme::SURFACE));
    onButton_.setColour(juce::TextButton::buttonOnColourId,
                        DarkTheme::getColour(DarkTheme::STATUS_SUCCESS));
    onButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    onButton_.setColour(juce::TextButton::textColourOnId,
                        DarkTheme::getColour(DarkTheme::BACKGROUND));
    onButton_.setClickingTogglesState(true);
    onButton_.setToggleState(!chain.muted, juce::dontSendNotification);  // On = not bypassed
    onButton_.onClick = [this]() { onBypassClicked(); };
    onButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addAndMakeVisible(onButton_);
}

ChainRowComponent::~ChainRowComponent() = default;

void ChainRowComponent::paint(juce::Graphics& g) {
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

void ChainRowComponent::mouseDown(const juce::MouseEvent& /*event*/) {
    if (onSelected) {
        onSelected(*this);
    }
}

void ChainRowComponent::setSelected(bool selected) {
    if (selected_ != selected) {
        selected_ = selected;
        repaint();
    }
}

void ChainRowComponent::resized() {
    auto bounds = getLocalBounds().reduced(3, 2);

    // Layout: [Name] [Gain] [Pan] [M] [S] [On]
    nameLabel_.setBounds(bounds.removeFromLeft(50));
    bounds.removeFromLeft(4);

    gainSlider_.setBounds(bounds.removeFromLeft(40));
    bounds.removeFromLeft(4);

    panSlider_.setBounds(bounds.removeFromLeft(35));
    bounds.removeFromLeft(4);

    muteButton_.setBounds(bounds.removeFromLeft(16));
    bounds.removeFromLeft(2);

    soloButton_.setBounds(bounds.removeFromLeft(16));
    bounds.removeFromLeft(2);

    onButton_.setBounds(bounds.removeFromLeft(22));
}

int ChainRowComponent::getPreferredHeight() const {
    return ROW_HEIGHT;
}

void ChainRowComponent::updateFromChain(const magda::ChainInfo& chain) {
    nameLabel_.setText(chain.name, juce::dontSendNotification);
    muteButton_.setToggleState(chain.muted, juce::dontSendNotification);
    soloButton_.setToggleState(chain.solo, juce::dontSendNotification);
    gainSlider_.setValue(chain.volume, juce::dontSendNotification);
    panSlider_.setValue(chain.pan, juce::dontSendNotification);
    onButton_.setToggleState(!chain.muted, juce::dontSendNotification);
}

void ChainRowComponent::onMuteClicked() {
    magda::TrackManager::getInstance().setChainMuted(trackId_, rackId_, chainId_,
                                                     muteButton_.getToggleState());
}

void ChainRowComponent::onSoloClicked() {
    magda::TrackManager::getInstance().setChainSolo(trackId_, rackId_, chainId_,
                                                    soloButton_.getToggleState());
}

void ChainRowComponent::onBypassClicked() {
    // "On" button: when ON, chain is not bypassed; when OFF, chain is bypassed
    // This is the inverse of mute, but could be a separate bypass flag
    // For now, we'll treat it as a visual indicator (could link to a bypass field)
}

}  // namespace magda::daw::ui
