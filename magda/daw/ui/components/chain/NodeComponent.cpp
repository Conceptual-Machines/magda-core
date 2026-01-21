#include "NodeComponent.hpp"

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

NodeComponent::NodeComponent() {
    // === HEADER ===

    // Bypass button
    bypassButton_.setButtonText("B");
    bypassButton_.setColour(juce::TextButton::buttonColourId,
                            DarkTheme::getColour(DarkTheme::SURFACE));
    bypassButton_.setColour(juce::TextButton::buttonOnColourId,
                            DarkTheme::getColour(DarkTheme::STATUS_WARNING));
    bypassButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    bypassButton_.setColour(juce::TextButton::textColourOnId,
                            DarkTheme::getColour(DarkTheme::BACKGROUND));
    bypassButton_.setClickingTogglesState(true);
    bypassButton_.onClick = [this]() {
        if (onBypassChanged) {
            onBypassChanged(bypassButton_.getToggleState());
        }
    };
    bypassButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addAndMakeVisible(bypassButton_);

    // Name label
    nameLabel_.setFont(FontManager::getInstance().getUIFontBold(10.0f));
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    nameLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(nameLabel_);

    // Delete button
    deleteButton_.setButtonText(juce::String::fromUTF8("\xc3\x97"));
    deleteButton_.setColour(juce::TextButton::buttonColourId,
                            DarkTheme::getColour(DarkTheme::SURFACE));
    deleteButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    deleteButton_.onClick = [this]() {
        if (onDeleteClicked) {
            onDeleteClicked();
        }
    };
    deleteButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addAndMakeVisible(deleteButton_);

    // === FOOTER ===

    // Modulator toggle button
    modToggleButton_.setButtonText("M");
    modToggleButton_.setColour(juce::TextButton::buttonColourId,
                               DarkTheme::getColour(DarkTheme::SURFACE));
    modToggleButton_.setColour(juce::TextButton::buttonOnColourId,
                               DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    modToggleButton_.setColour(juce::TextButton::textColourOffId,
                               DarkTheme::getSecondaryTextColour());
    modToggleButton_.setColour(juce::TextButton::textColourOnId,
                               DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    modToggleButton_.setClickingTogglesState(true);
    modToggleButton_.onClick = [this]() {
        modPanelVisible_ = modToggleButton_.getToggleState();
        if (onModPanelToggled) {
            onModPanelToggled(modPanelVisible_);
        }
        resized();
        repaint();
        if (onLayoutChanged) {
            onLayoutChanged();
        }
    };
    modToggleButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addAndMakeVisible(modToggleButton_);

    // Params toggle button
    paramToggleButton_.setButtonText("P");
    paramToggleButton_.setColour(juce::TextButton::buttonColourId,
                                 DarkTheme::getColour(DarkTheme::SURFACE));
    paramToggleButton_.setColour(juce::TextButton::buttonOnColourId,
                                 DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    paramToggleButton_.setColour(juce::TextButton::textColourOffId,
                                 DarkTheme::getSecondaryTextColour());
    paramToggleButton_.setColour(juce::TextButton::textColourOnId,
                                 DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    paramToggleButton_.setClickingTogglesState(true);
    paramToggleButton_.onClick = [this]() {
        paramPanelVisible_ = paramToggleButton_.getToggleState();
        if (onParamPanelToggled) {
            onParamPanelToggled(paramPanelVisible_);
        }
        resized();
        repaint();
        if (onLayoutChanged) {
            onLayoutChanged();
        }
    };
    paramToggleButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addAndMakeVisible(paramToggleButton_);

    // Gain toggle button
    gainToggleButton_.setButtonText("G");
    gainToggleButton_.setColour(juce::TextButton::buttonColourId,
                                DarkTheme::getColour(DarkTheme::SURFACE));
    gainToggleButton_.setColour(juce::TextButton::buttonOnColourId,
                                DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    gainToggleButton_.setColour(juce::TextButton::textColourOffId,
                                DarkTheme::getSecondaryTextColour());
    gainToggleButton_.setColour(juce::TextButton::textColourOnId,
                                DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    gainToggleButton_.setClickingTogglesState(true);
    gainToggleButton_.onClick = [this]() {
        gainPanelVisible_ = gainToggleButton_.getToggleState();
        if (onGainPanelToggled) {
            onGainPanelToggled(gainPanelVisible_);
        }
        resized();
        repaint();
        if (onLayoutChanged) {
            onLayoutChanged();
        }
    };
    gainToggleButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addAndMakeVisible(gainToggleButton_);
}

NodeComponent::~NodeComponent() = default;

void NodeComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // === LEFT SIDE PANELS: [Mods][Params] ===
    if (modPanelVisible_) {
        auto modArea = bounds.removeFromLeft(getModPanelWidth());
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
        g.fillRoundedRectangle(modArea.toFloat(), 4.0f);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRoundedRectangle(modArea.toFloat(), 4.0f, 1.0f);
        paintModPanel(g, modArea);
    }

    if (paramPanelVisible_) {
        auto paramArea = bounds.removeFromLeft(getParamPanelWidth());
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
        g.fillRoundedRectangle(paramArea.toFloat(), 4.0f);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRoundedRectangle(paramArea.toFloat(), 4.0f, 1.0f);
        paintParamPanel(g, paramArea);
    }

    // === RIGHT SIDE PANEL: [Gain] ===
    if (gainPanelVisible_) {
        auto gainArea = bounds.removeFromRight(getGainPanelWidth());
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
        g.fillRoundedRectangle(gainArea.toFloat(), 4.0f);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRoundedRectangle(gainArea.toFloat(), 4.0f, 1.0f);
        paintGainPanel(g, gainArea);
    }

    // === MAIN NODE AREA (remaining bounds) ===
    // Background
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.03f));
    g.fillRoundedRectangle(bounds.toFloat(), 4.0f);

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRoundedRectangle(bounds.toFloat(), 4.0f, 1.0f);

    // Header separator
    g.drawHorizontalLine(HEADER_HEIGHT, static_cast<float>(bounds.getX()),
                         static_cast<float>(bounds.getRight()));

    // Footer separator
    g.drawHorizontalLine(getHeight() - FOOTER_HEIGHT, static_cast<float>(bounds.getX()),
                         static_cast<float>(bounds.getRight()));

    // Calculate content area (between header and footer)
    auto contentArea = bounds;
    contentArea.removeFromTop(HEADER_HEIGHT);
    contentArea.removeFromBottom(FOOTER_HEIGHT);

    // Let subclass paint main content
    paintContent(g, contentArea);

    // Dim if bypassed (draw over everything)
    if (bypassButton_.getToggleState()) {
        g.setColour(juce::Colours::black.withAlpha(0.3f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    }
}

void NodeComponent::resized() {
    auto bounds = getLocalBounds();

    // === LEFT SIDE PANELS: [Mods][Params] ===
    if (modPanelVisible_) {
        auto modArea = bounds.removeFromLeft(getModPanelWidth());
        resizedModPanel(modArea);
    }

    if (paramPanelVisible_) {
        auto paramArea = bounds.removeFromLeft(getParamPanelWidth());
        resizedParamPanel(paramArea);
    }

    // === RIGHT SIDE PANEL: [Gain] ===
    if (gainPanelVisible_) {
        auto gainArea = bounds.removeFromRight(getGainPanelWidth());
        resizedGainPanel(gainArea);
    }

    // === MAIN NODE AREA (remaining bounds) ===

    // === HEADER: [B] Name ... [X] ===
    auto headerArea = bounds.removeFromTop(HEADER_HEIGHT).reduced(3, 2);
    bypassButton_.setBounds(headerArea.removeFromLeft(BUTTON_SIZE));
    headerArea.removeFromLeft(4);
    deleteButton_.setBounds(headerArea.removeFromRight(BUTTON_SIZE));
    headerArea.removeFromRight(4);

    // Let subclass add extra header buttons
    resizedHeaderExtra(headerArea);

    nameLabel_.setBounds(headerArea);

    // === FOOTER: [M] [P] ... [G] ===
    auto footerArea = bounds.removeFromBottom(FOOTER_HEIGHT).reduced(3, 2);
    modToggleButton_.setBounds(footerArea.removeFromLeft(BUTTON_SIZE));
    footerArea.removeFromLeft(2);
    paramToggleButton_.setBounds(footerArea.removeFromLeft(BUTTON_SIZE));
    gainToggleButton_.setBounds(footerArea.removeFromRight(BUTTON_SIZE));

    // === CONTENT (remaining area) ===
    auto contentArea = bounds.reduced(2, 0);
    resizedContent(contentArea);
}

void NodeComponent::setNodeName(const juce::String& name) {
    nameLabel_.setText(name, juce::dontSendNotification);
}

juce::String NodeComponent::getNodeName() const {
    return nameLabel_.getText();
}

void NodeComponent::setBypassed(bool bypassed) {
    bypassButton_.setToggleState(bypassed, juce::dontSendNotification);
}

bool NodeComponent::isBypassed() const {
    return bypassButton_.getToggleState();
}

void NodeComponent::paintContent(juce::Graphics& /*g*/, juce::Rectangle<int> /*contentArea*/) {
    // Default: nothing - subclasses override
}

void NodeComponent::resizedContent(juce::Rectangle<int> /*contentArea*/) {
    // Default: nothing - subclasses override
}

void NodeComponent::resizedHeaderExtra(juce::Rectangle<int>& /*headerArea*/) {
    // Default: nothing - subclasses override to add extra header buttons
}

int NodeComponent::getLeftPanelsWidth() const {
    int width = 0;
    if (modPanelVisible_)
        width += getModPanelWidth();
    if (paramPanelVisible_)
        width += getParamPanelWidth();
    return width;
}

int NodeComponent::getRightPanelsWidth() const {
    int width = 0;
    if (gainPanelVisible_)
        width += getGainPanelWidth();
    return width;
}

int NodeComponent::getTotalWidth(int baseContentWidth) const {
    return getLeftPanelsWidth() + baseContentWidth + getRightPanelsWidth();
}

void NodeComponent::paintModPanel(juce::Graphics& g, juce::Rectangle<int> panelArea) {
    // Default: draw labeled placeholder (vertical side panel)
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    g.setFont(FontManager::getInstance().getUIFont(8.0f));
    g.drawText("MOD", panelArea.removeFromTop(16), juce::Justification::centred);
}

void NodeComponent::paintParamPanel(juce::Graphics& g, juce::Rectangle<int> panelArea) {
    // Default: draw labeled placeholder (vertical side panel)
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    g.setFont(FontManager::getInstance().getUIFont(8.0f));
    g.drawText("PRM", panelArea.removeFromTop(16), juce::Justification::centred);
}

void NodeComponent::paintGainPanel(juce::Graphics& g, juce::Rectangle<int> panelArea) {
    // Draw a vertical meter/slider representation
    auto meterArea = panelArea.reduced(4, 8);

    // Meter background
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND));
    g.fillRoundedRectangle(meterArea.toFloat(), 2.0f);

    // Mock meter fill (would be driven by actual audio level)
    float meterLevel = 0.6f;
    int fillHeight = static_cast<int>(meterLevel * meterArea.getHeight());
    auto fillArea = meterArea.removeFromBottom(fillHeight);

    // Gradient from green to yellow to red
    juce::ColourGradient gradient(
        juce::Colour(0xff2ecc71), 0.0f, static_cast<float>(meterArea.getBottom()),
        juce::Colour(0xffe74c3c), 0.0f, static_cast<float>(meterArea.getY()), false);
    gradient.addColour(0.7, juce::Colour(0xfff39c12));
    g.setGradientFill(gradient);
    g.fillRect(fillArea);

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRoundedRectangle(panelArea.reduced(4, 8).toFloat(), 2.0f, 1.0f);
}

void NodeComponent::resizedModPanel(juce::Rectangle<int> /*panelArea*/) {
    // Default: nothing - subclasses override
}

void NodeComponent::resizedParamPanel(juce::Rectangle<int> /*panelArea*/) {
    // Default: nothing - subclasses override
}

void NodeComponent::resizedGainPanel(juce::Rectangle<int> /*panelArea*/) {
    // Default: nothing - subclasses override
}

}  // namespace magda::daw::ui
