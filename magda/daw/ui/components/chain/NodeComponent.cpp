#include "NodeComponent.hpp"

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

NodeComponent::NodeComponent() {
    // === HEADER ===

    // Bypass button (power symbol)
    bypassButton_.setButtonText(juce::String::fromUTF8("\xe2\x8f\xbb"));  // ⏻ power symbol
    // OFF state (not bypassed = active) = green background
    bypassButton_.setColour(juce::TextButton::buttonColourId,
                            DarkTheme::getColour(DarkTheme::ACCENT_GREEN).darker(0.3f));
    // ON state (bypassed) = reddish background
    bypassButton_.setColour(juce::TextButton::buttonOnColourId,
                            DarkTheme::getColour(DarkTheme::STATUS_ERROR));
    bypassButton_.setColour(juce::TextButton::textColourOffId,
                            DarkTheme::getColour(DarkTheme::BACKGROUND));
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

    // Delete button (reddish background)
    deleteButton_.setButtonText(juce::String::fromUTF8("\xc3\x97"));  // × symbol
    deleteButton_.setColour(juce::TextButton::buttonColourId,
                            DarkTheme::getColour(DarkTheme::STATUS_ERROR).darker(0.2f));
    deleteButton_.setColour(juce::TextButton::textColourOffId,
                            DarkTheme::getColour(DarkTheme::BACKGROUND));
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

    // === MOD PANEL CONTROLS ===
    for (int i = 0; i < 3; ++i) {
        modSlotButtons_[i] = std::make_unique<juce::TextButton>("+");
        modSlotButtons_[i]->setColour(juce::TextButton::buttonColourId,
                                      DarkTheme::getColour(DarkTheme::SURFACE));
        modSlotButtons_[i]->setColour(juce::TextButton::textColourOffId,
                                      DarkTheme::getSecondaryTextColour());
        modSlotButtons_[i]->onClick = [this, i]() {
            juce::PopupMenu menu;
            menu.addItem(1, "LFO");
            menu.addItem(2, "Bezier LFO");
            menu.addItem(3, "ADSR");
            menu.addItem(4, "Envelope Follower");
            menu.showMenuAsync(juce::PopupMenu::Options(), [this, i](int result) {
                if (result > 0) {
                    juce::StringArray types = {"", "LFO", "BEZ", "ADSR", "ENV"};
                    modSlotButtons_[i]->setButtonText(types[result]);
                }
            });
        };
        addChildComponent(*modSlotButtons_[i]);
    }

    // === PARAM PANEL CONTROLS ===
    for (int i = 0; i < 4; ++i) {
        auto knob = std::make_unique<juce::Slider>();
        knob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        knob->setRange(0.0, 1.0, 0.01);
        knob->setValue(0.5);
        knob->setColour(juce::Slider::rotarySliderFillColourId,
                        DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
        knob->setColour(juce::Slider::rotarySliderOutlineColourId,
                        DarkTheme::getColour(DarkTheme::SURFACE));
        addChildComponent(*knob);
        paramKnobs_.push_back(std::move(knob));
    }
}

NodeComponent::~NodeComponent() = default;

void NodeComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // === LEFT SIDE PANELS: [Mods][Params] (squared corners) ===
    if (modPanelVisible_) {
        auto modArea = bounds.removeFromLeft(getModPanelWidth());
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
        g.fillRect(modArea);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRect(modArea);
        paintModPanel(g, modArea);
    }

    if (paramPanelVisible_) {
        auto paramArea = bounds.removeFromLeft(getParamPanelWidth());
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
        g.fillRect(paramArea);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRect(paramArea);
        paintParamPanel(g, paramArea);
    }

    // === RIGHT SIDE PANEL: [Gain] (squared corners) ===
    if (gainPanelVisible_) {
        auto gainArea = bounds.removeFromRight(getGainPanelWidth());
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
        g.fillRect(gainArea);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRect(gainArea);
        paintGainPanel(g, gainArea);
    }

    // === MAIN NODE AREA (remaining bounds) ===
    // Background
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.03f));
    g.fillRoundedRectangle(bounds.toFloat(), 4.0f);

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRoundedRectangle(bounds.toFloat(), 4.0f, 1.0f);

    // Header separator (only if header visible)
    int headerHeight = getHeaderHeight();
    if (headerHeight > 0) {
        g.drawHorizontalLine(headerHeight, static_cast<float>(bounds.getX()),
                             static_cast<float>(bounds.getRight()));
    }

    // Footer separator
    g.drawHorizontalLine(getHeight() - FOOTER_HEIGHT, static_cast<float>(bounds.getX()),
                         static_cast<float>(bounds.getRight()));

    // Calculate content area (between header and footer)
    auto contentArea = bounds;
    contentArea.removeFromTop(headerHeight);
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
    } else {
        // Hide mod slot buttons when panel is not visible
        for (auto& btn : modSlotButtons_) {
            if (btn)
                btn->setVisible(false);
        }
    }

    if (paramPanelVisible_) {
        auto paramArea = bounds.removeFromLeft(getParamPanelWidth());
        resizedParamPanel(paramArea);
    } else {
        // Hide param knobs when panel is not visible
        for (auto& knob : paramKnobs_) {
            knob->setVisible(false);
        }
    }

    // === RIGHT SIDE PANEL: [Gain] ===
    if (gainPanelVisible_) {
        auto gainArea = bounds.removeFromRight(getGainPanelWidth());
        resizedGainPanel(gainArea);
    }

    // === MAIN NODE AREA (remaining bounds) ===

    // === HEADER: [B] Name ... [X] === (only if header visible)
    int headerHeight = getHeaderHeight();
    if (headerHeight > 0) {
        auto headerArea = bounds.removeFromTop(headerHeight).reduced(3, 2);
        bypassButton_.setBounds(headerArea.removeFromLeft(BUTTON_SIZE));
        headerArea.removeFromLeft(4);
        deleteButton_.setBounds(headerArea.removeFromRight(BUTTON_SIZE));
        headerArea.removeFromRight(4);

        // Let subclass add extra header buttons
        resizedHeaderExtra(headerArea);

        nameLabel_.setBounds(headerArea);

        bypassButton_.setVisible(true);
        deleteButton_.setVisible(true);
        nameLabel_.setVisible(true);
    } else {
        // Hide header controls
        bypassButton_.setVisible(false);
        deleteButton_.setVisible(false);
        nameLabel_.setVisible(false);
    }

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

void NodeComponent::setParamButtonVisible(bool visible) {
    paramToggleButton_.setVisible(visible);
}

void NodeComponent::setModButtonVisible(bool visible) {
    modToggleButton_.setVisible(visible);
}

void NodeComponent::setGainButtonVisible(bool visible) {
    gainToggleButton_.setVisible(visible);
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

void NodeComponent::resizedModPanel(juce::Rectangle<int> panelArea) {
    panelArea.removeFromTop(16);  // Skip label
    panelArea = panelArea.reduced(2);

    int slotHeight = (panelArea.getHeight() - 4) / 3;
    for (int i = 0; i < 3; ++i) {
        modSlotButtons_[i]->setBounds(panelArea.removeFromTop(slotHeight).reduced(0, 1));
        modSlotButtons_[i]->setVisible(true);
    }
}

void NodeComponent::resizedParamPanel(juce::Rectangle<int> panelArea) {
    panelArea.removeFromTop(16);  // Skip label
    panelArea = panelArea.reduced(2);

    int knobSize = (panelArea.getWidth() - 2) / 2;
    int row = 0, col = 0;
    for (auto& knob : paramKnobs_) {
        int x = panelArea.getX() + col * (knobSize + 2);
        int y = panelArea.getY() + row * (knobSize + 2);
        knob->setBounds(x, y, knobSize, knobSize);
        knob->setVisible(true);
        col++;
        if (col >= 2) {
            col = 0;
            row++;
        }
    }
}

void NodeComponent::resizedGainPanel(juce::Rectangle<int> /*panelArea*/) {
    // Default: nothing - gain meter drawn in paintGainPanel
}

}  // namespace magda::daw::ui
