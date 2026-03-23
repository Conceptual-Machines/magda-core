#include "ChainRowComponent.hpp"

#include <BinaryData.h>

#include "RackComponent.hpp"
#include "core/SelectionManager.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

ChainRowComponent::ChainRowComponent(RackComponent& owner, magda::TrackId trackId,
                                     magda::RackId rackId, const magda::ChainInfo& chain)
    : owner_(owner), trackId_(trackId), rackId_(rackId), chainId_(chain.id) {
    // Set up node path for centralized selection
    nodePath_ = magda::ChainNodePath::chain(trackId, rackId, chain.id);

    // Register as SelectionManager listener
    magda::SelectionManager::getInstance().addListener(this);
    // Name label - clicks pass through to parent for selection
    nameLabel_.setText(chain.name, juce::dontSendNotification);
    nameLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    nameLabel_.setJustificationType(juce::Justification::centredLeft);
    nameLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(nameLabel_);

    // Gain text slider (dB format)
    gainSlider_.setFormat(TextSlider::Format::Decibels);
    gainSlider_.setRange(-60.0, 6.0, 0.1);
    gainSlider_.setValue(chain.volume, juce::dontSendNotification);
    gainSlider_.onValueChanged = [this](double value) {
        magda::TrackManager::getInstance().setChainVolume(trackId_, rackId_, chainId_,
                                                          static_cast<float>(value));
    };
    addAndMakeVisible(gainSlider_);

    // Pan text slider (L/C/R format)
    panSlider_.setFormat(TextSlider::Format::Pan);
    panSlider_.setRange(-1.0, 1.0, 0.01);
    panSlider_.setValue(chain.pan, juce::dontSendNotification);
    panSlider_.onValueChanged = [this](double value) {
        magda::TrackManager::getInstance().setChainPan(trackId_, rackId_, chainId_,
                                                       static_cast<float>(value));
    };
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

    // On/bypass button (power icon)
    onButton_ = std::make_unique<magda::SvgButton>("Power", BinaryData::power_on_svg,
                                                   BinaryData::power_on_svgSize);
    onButton_->setClickingTogglesState(true);
    onButton_->setToggleState(!chain.bypassed, juce::dontSendNotification);  // On = not bypassed
    onButton_->setNormalColor(DarkTheme::getColour(DarkTheme::STATUS_ERROR));
    onButton_->setActiveColor(juce::Colours::white);
    onButton_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_GREEN).darker(0.3f));
    onButton_->setActive(!chain.bypassed);
    onButton_->onClick = [this]() {
        onButton_->setActive(onButton_->getToggleState());
        onBypassClicked();
    };
    addAndMakeVisible(*onButton_);

    // Delete button (reddish-purple background)
    deleteButton_.setButtonText(juce::String::fromUTF8("\xc3\x97"));  // × symbol
    deleteButton_.setColour(
        juce::TextButton::buttonColourId,
        DarkTheme::getColour(DarkTheme::ACCENT_PURPLE)
            .interpolatedWith(DarkTheme::getColour(DarkTheme::STATUS_ERROR), 0.5f)
            .darker(0.2f));
    deleteButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    deleteButton_.onClick = [this]() { onDeleteClicked(); };
    deleteButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addAndMakeVisible(deleteButton_);

    // Dim controls if chain starts bypassed
    if (chain.bypassed) {
        float alpha = 0.35f;
        nameLabel_.setAlpha(alpha);
        muteButton_.setAlpha(alpha);
        soloButton_.setAlpha(alpha);
        gainSlider_.setAlpha(alpha);
        panSlider_.setAlpha(alpha);
    }
}

ChainRowComponent::~ChainRowComponent() {
    magda::SelectionManager::getInstance().removeListener(this);
}

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
    // Just visual feedback - actual selection happens on mouseUp to avoid
    // issues with multiple mouseDown events during layout changes
}

void ChainRowComponent::mouseUp(const juce::MouseEvent& event) {
    // Only handle if mouse is still over this component (user didn't drag away)
    if (!contains(event.getPosition())) {
        return;
    }

    DBG("ChainRowComponent::mouseUp - chainId=" << chainId_ << " rackId=" << rackId_);

    // Use centralized selection - RackComponent will respond via chainNodeSelectionChanged
    magda::SelectionManager::getInstance().selectChainNode(nodePath_);
}

void ChainRowComponent::mouseDoubleClick(const juce::MouseEvent& /*event*/) {
    // Double-click toggles expand/collapse of this chain
    if (onDoubleClick) {
        onDoubleClick(chainId_);
    }
}

void ChainRowComponent::selectionTypeChanged(magda::SelectionType /*newType*/) {
    // Selection type changed - chainNodeSelectionChanged will handle visual update
}

void ChainRowComponent::chainNodeSelectionChanged(const magda::ChainNodePath& path) {
    // Update our selection state based on whether we match the selected path
    bool shouldBeSelected = nodePath_.isValid() && nodePath_ == path;
    DBG("ChainRowComponent::chainNodeSelectionChanged - chainId="
        << chainId_ << " shouldBeSelected=" << (shouldBeSelected ? "yes" : "no")
        << " currentSelected=" << (selected_ ? "yes" : "no"));
    setSelected(shouldBeSelected);
}

void ChainRowComponent::setSelected(bool selected) {
    if (selected_ != selected) {
        selected_ = selected;
        repaint();
    }
}

void ChainRowComponent::setNodePath(const magda::ChainNodePath& path) {
    nodePath_ = path;

    // Check if this chain is currently selected in SelectionManager
    // This handles the case where selection happened before the row was created
    const auto& currentSelection = magda::SelectionManager::getInstance().getSelectedChainNode();
    if (currentSelection.isValid() && currentSelection == nodePath_) {
        setSelected(true);
    }
}

void ChainRowComponent::resized() {
    auto bounds = getLocalBounds().reduced(3, 2);

    // Layout: [Name] [Gain] [Pan] ... [M] [S] [On] [X]
    // Spread across full width with right-side buttons anchored to the right

    // Right side buttons (from right to left)
    deleteButton_.setBounds(bounds.removeFromRight(16));
    bounds.removeFromRight(2);

    onButton_->setBounds(bounds.removeFromRight(16));
    bounds.removeFromRight(2);

    soloButton_.setBounds(bounds.removeFromRight(16));
    bounds.removeFromRight(2);

    muteButton_.setBounds(bounds.removeFromRight(16));
    bounds.removeFromRight(8);

    // Left side elements
    nameLabel_.setBounds(bounds.removeFromLeft(50));
    bounds.removeFromLeft(4);

    // Remaining space for gain and pan sliders (spread them out)
    int remainingWidth = bounds.getWidth();
    int sliderWidth = (remainingWidth - 8) / 2;  // Split remaining space, minus gap

    gainSlider_.setBounds(bounds.removeFromLeft(sliderWidth));
    bounds.removeFromLeft(8);

    panSlider_.setBounds(bounds.removeFromLeft(sliderWidth));
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
    onButton_->setToggleState(!chain.bypassed, juce::dontSendNotification);
    onButton_->setActive(!chain.bypassed);

    // Dim controls when chain is bypassed
    float alpha = chain.bypassed ? 0.35f : 1.0f;
    nameLabel_.setAlpha(alpha);
    muteButton_.setAlpha(alpha);
    soloButton_.setAlpha(alpha);
    gainSlider_.setAlpha(alpha);
    panSlider_.setAlpha(alpha);
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
    bool active = onButton_->getToggleState();
    magda::TrackManager::getInstance().setChainBypassed(trackId_, rackId_, chainId_, !active);
}

void ChainRowComponent::onDeleteClicked() {
    // Use path-based removal to support nested chains
    if (nodePath_.isValid()) {
        magda::TrackManager::getInstance().removeChainByPath(nodePath_);
    } else {
        // Fallback to flat ID removal for top-level chains
        magda::TrackManager::getInstance().removeChainFromRack(trackId_, rackId_, chainId_);
    }
}

}  // namespace magda::daw::ui
