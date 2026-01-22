#include "NodeComponent.hpp"

#include <BinaryData.h>

#include "core/SelectionManager.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

NodeComponent::NodeComponent() {
    // Register as SelectionManager listener for centralized selection
    magda::SelectionManager::getInstance().addListener(this);
    // === HEADER ===

    // Bypass button (power icon)
    bypassButton_ = std::make_unique<magda::SvgButton>("Power", BinaryData::power_on_svg,
                                                       BinaryData::power_on_svgSize);
    bypassButton_->setClickingTogglesState(true);
    bypassButton_->setNormalColor(DarkTheme::getColour(DarkTheme::STATUS_ERROR));
    bypassButton_->setActiveColor(juce::Colours::white);
    bypassButton_->setActiveBackgroundColor(
        DarkTheme::getColour(DarkTheme::ACCENT_GREEN).darker(0.3f));
    bypassButton_->setActive(true);  // Default: not bypassed = active
    bypassButton_->onClick = [this]() {
        bool bypassed = !bypassButton_->getToggleState();  // Toggle OFF = bypassed
        bypassButton_->setActive(!bypassed);
        if (onBypassChanged) {
            onBypassChanged(bypassed);
        }
    };
    addAndMakeVisible(*bypassButton_);

    // Name label - clicks pass through for selection
    nameLabel_.setFont(FontManager::getInstance().getUIFontBold(10.0f));
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    nameLabel_.setJustificationType(juce::Justification::centredLeft);
    nameLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(nameLabel_);

    // Delete button (reddish-purple background)
    deleteButton_.setButtonText(juce::String::fromUTF8("\xc3\x97"));  // × symbol
    deleteButton_.setColour(
        juce::TextButton::buttonColourId,
        DarkTheme::getColour(DarkTheme::ACCENT_PURPLE)
            .interpolatedWith(DarkTheme::getColour(DarkTheme::STATUS_ERROR), 0.5f)
            .darker(0.2f));
    deleteButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
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

NodeComponent::~NodeComponent() {
    magda::SelectionManager::getInstance().removeListener(this);
}

void NodeComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // When collapsed, draw a narrow vertical strip with rotated name
    // BUT still draw side panels if visible
    if (collapsed_) {
        // === LEFT SIDE PANELS (even when collapsed) ===
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

        // === RIGHT SIDE PANEL (even when collapsed) ===
        if (gainPanelVisible_) {
            auto gainArea = bounds.removeFromRight(getGainPanelWidth());
            g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
            g.fillRect(gainArea);
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
            g.drawRect(gainArea);
            paintGainPanel(g, gainArea);
        }

        // === COLLAPSED MAIN STRIP (remaining bounds) ===
        // Background
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.03f));
        g.fillRoundedRectangle(bounds.toFloat(), 4.0f);

        // Border
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRoundedRectangle(bounds.toFloat(), 4.0f, 1.0f);

        // Draw name vertically (rotated 90 degrees)
        g.saveState();
        g.setColour(DarkTheme::getTextColour());
        g.setFont(FontManager::getInstance().getUIFontBold(10.0f));

        // Rotate around center and draw text
        auto center = bounds.getCentre().toFloat();
        g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi,
                                                       center.x, center.y));
        // Draw text centered (swapped width/height due to rotation)
        juce::Rectangle<int> textBounds(static_cast<int>(center.x - bounds.getHeight() / 2),
                                        static_cast<int>(center.y - bounds.getWidth() / 2),
                                        bounds.getHeight(), bounds.getWidth());
        g.drawText(getNodeName(), textBounds, juce::Justification::centred);
        g.restoreState();

        // Dim if bypassed
        if (!bypassButton_->getToggleState()) {
            g.setColour(juce::Colours::black.withAlpha(0.3f));
            g.fillRoundedRectangle(bounds.toFloat(), 4.0f);
        }

        // Selection border (around main strip only)
        if (selected_) {
            g.setColour(juce::Colour(0xff888888));
            g.drawRoundedRectangle(bounds.toFloat().reduced(1.0f), 4.0f, 2.0f);
        }
        return;
    }

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

    // Footer separator (only if footer visible)
    int footerHeight = getFooterHeight();
    if (footerHeight > 0) {
        g.drawHorizontalLine(getHeight() - footerHeight, static_cast<float>(bounds.getX()),
                             static_cast<float>(bounds.getRight()));
    }

    // Calculate content area (between header and footer)
    auto contentArea = bounds;
    contentArea.removeFromTop(headerHeight);
    contentArea.removeFromBottom(footerHeight);

    // Let subclass paint main content
    paintContent(g, contentArea);

    // Dim if bypassed (draw over everything)
    if (!bypassButton_->getToggleState()) {  // Toggle OFF = bypassed
        g.setColour(juce::Colours::black.withAlpha(0.3f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    }

    // Selection border (draw on top of everything)
    if (selected_) {
        g.setColour(juce::Colour(0xff888888));  // Grey
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 4.0f, 2.0f);
    }
}

void NodeComponent::resized() {
    auto bounds = getLocalBounds();

    // When collapsed (narrow width), arrange key icons vertically
    // BUT still layout side panels if visible
    if (collapsed_) {
        // === LEFT SIDE PANELS (even when collapsed) ===
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

        // === RIGHT SIDE PANEL (even when collapsed) ===
        if (gainPanelVisible_) {
            auto gainArea = bounds.removeFromRight(getGainPanelWidth());
            resizedGainPanel(gainArea);
        }

        // === COLLAPSED MAIN STRIP (remaining bounds) ===
        // Hide footer panel toggle buttons
        modToggleButton_.setVisible(false);
        paramToggleButton_.setVisible(false);
        gainToggleButton_.setVisible(false);
        nameLabel_.setVisible(false);

        // Arrange buttons vertically at top of collapsed strip
        auto area = bounds.reduced(4);
        int buttonSize = juce::jmin(BUTTON_SIZE, area.getWidth() - 4);

        // Delete button at top (always visible)
        deleteButton_.setBounds(
            area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
        deleteButton_.setVisible(true);
        area.removeFromTop(4);

        // Bypass button below delete (only if it was visible - devices use their own)
        if (bypassButton_->isVisible()) {
            bypassButton_->setBounds(
                area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
            area.removeFromTop(4);
        }

        // Let subclass add extra collapsed buttons
        resizedCollapsed(area);

        // Call resizedContent with empty area so subclasses can hide their content
        resizedContent(juce::Rectangle<int>());
        return;
    }

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

        // Delete button on far right (if visible)
        if (deleteButton_.isVisible()) {
            deleteButton_.setBounds(headerArea.removeFromRight(BUTTON_SIZE));
            headerArea.removeFromRight(4);
        }

        // Bypass/power button next to delete (if visible)
        if (bypassButton_->isVisible()) {
            bypassButton_->setBounds(headerArea.removeFromRight(BUTTON_SIZE));
            headerArea.removeFromRight(4);
        }

        // Let subclass add extra header buttons
        resizedHeaderExtra(headerArea);

        nameLabel_.setBounds(headerArea);
        nameLabel_.setVisible(true);
    } else {
        // Hide header controls
        bypassButton_->setVisible(false);
        deleteButton_.setVisible(false);
        nameLabel_.setVisible(false);
    }

    // === FOOTER: [M] [P] ... [G] === (only if footer visible)
    int footerHeight = getFooterHeight();
    if (footerHeight > 0) {
        auto footerArea = bounds.removeFromBottom(footerHeight).reduced(3, 2);
        modToggleButton_.setBounds(footerArea.removeFromLeft(BUTTON_SIZE));
        footerArea.removeFromLeft(2);
        paramToggleButton_.setBounds(footerArea.removeFromLeft(BUTTON_SIZE));
        gainToggleButton_.setBounds(footerArea.removeFromRight(BUTTON_SIZE));

        modToggleButton_.setVisible(true);
        paramToggleButton_.setVisible(
            paramToggleButton_.isVisible());  // Respect setParamButtonVisible
        gainToggleButton_.setVisible(
            gainToggleButton_.isVisible());  // Respect setGainButtonVisible
    } else {
        // Hide footer controls
        modToggleButton_.setVisible(false);
        paramToggleButton_.setVisible(false);
        gainToggleButton_.setVisible(false);
    }

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
    bypassButton_->setToggleState(!bypassed, juce::dontSendNotification);  // Active = not bypassed
    bypassButton_->setActive(!bypassed);
}

bool NodeComponent::isBypassed() const {
    return !bypassButton_->getToggleState();  // Toggle OFF = bypassed
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

void NodeComponent::setBypassButtonVisible(bool visible) {
    bypassButton_->setVisible(visible);
}

void NodeComponent::setDeleteButtonVisible(bool visible) {
    deleteButton_.setVisible(visible);
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

void NodeComponent::resizedCollapsed(juce::Rectangle<int>& /*area*/) {
    // Default: nothing - subclasses can add extra buttons
}

void NodeComponent::setSelected(bool selected) {
    if (selected_ != selected) {
        selected_ = selected;
        repaint();
    }
}

void NodeComponent::setCollapsed(bool collapsed) {
    if (collapsed_ != collapsed) {
        collapsed_ = collapsed;
        resized();
        repaint();
        if (onCollapsedChanged) {
            onCollapsedChanged(collapsed_);
        }
        if (onLayoutChanged) {
            onLayoutChanged();
        }
    }
}

void NodeComponent::setNodePath(const magda::ChainNodePath& path) {
    nodePath_ = path;
}

void NodeComponent::selectionTypeChanged(magda::SelectionType /*newType*/) {
    // If selection type changed away from ChainNode, we might need to deselect
    // But chainNodeSelectionChanged handles this more precisely
}

void NodeComponent::chainNodeSelectionChanged(const magda::ChainNodePath& path) {
    // Update our selection state based on whether we match the selected path
    bool shouldBeSelected = nodePath_.isValid() && nodePath_ == path;
    setSelected(shouldBeSelected);
}

void NodeComponent::mouseDown(const juce::MouseEvent& e) {
    // Only handle left clicks for selection
    if (e.mods.isLeftButtonDown()) {
        mouseDownForSelection_ = true;
    }
}

void NodeComponent::mouseUp(const juce::MouseEvent& e) {
    // Complete selection on mouse up (click-and-release)
    if (mouseDownForSelection_ && !e.mods.isPopupMenu()) {
        mouseDownForSelection_ = false;

        // Check if mouse is still within bounds (not a drag-away)
        if (getLocalBounds().contains(e.getPosition())) {
            DBG("NodeComponent::mouseUp - name='" + getNodeName() +
                "' pathValid=" + juce::String(nodePath_.isValid() ? 1 : 0) + " trackId=" +
                juce::String(nodePath_.trackId) + " selected=" + juce::String(selected_ ? 1 : 0));

            // If already selected, toggle collapsed state
            if (selected_) {
                setCollapsed(!collapsed_);
            } else {
                // Use centralized selection if we have a valid path
                if (nodePath_.isValid()) {
                    magda::SelectionManager::getInstance().selectChainNode(nodePath_);
                } else {
                    DBG("  -> Path NOT valid, skipping centralized selection");
                }

                // Also call legacy callback for backward compatibility during transition
                if (onSelected) {
                    onSelected();
                }
            }
        }
    }
}

}  // namespace magda::daw::ui
