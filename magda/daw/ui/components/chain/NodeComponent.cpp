#include "NodeComponent.hpp"

#include <BinaryData.h>

#include <cstdlib>

#include "MacroEditorPanel.hpp"
#include "MacroPanelComponent.hpp"
#include "ModsPanelComponent.hpp"
#include "ModulatorEditorPanel.hpp"
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
        // === LEFT SIDE PANELS (even when collapsed): [Macros][MacroEditor][Mods][ModEditor] ===
        if (paramPanelVisible_) {
            auto paramArea = bounds.removeFromLeft(getParamPanelWidth());
            g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
            g.fillRect(paramArea);
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
            g.drawRect(paramArea);
            paintParamPanel(g, paramArea);
        }

        // Macro editor panel - after macros, before mods
        int extraRightWidthCollapsed = getExtraRightPanelWidth();
        if (extraRightWidthCollapsed > 0) {
            auto extraRightArea = bounds.removeFromLeft(extraRightWidthCollapsed);
            g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
            g.fillRect(extraRightArea);
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
            g.drawRect(extraRightArea);
            paintExtraRightPanel(g, extraRightArea);
        }

        if (modPanelVisible_) {
            auto modArea = bounds.removeFromLeft(getModPanelWidth());
            g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
            g.fillRect(modArea);
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
            g.drawRect(modArea);
            paintModPanel(g, modArea);
        }

        // Mod editor panel - after mods, before main content
        int extraWidthCollapsed = getExtraLeftPanelWidth();
        if (extraWidthCollapsed > 0) {
            auto extraArea = bounds.removeFromLeft(extraWidthCollapsed);
            g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
            g.fillRect(extraArea);
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
            g.drawRect(extraArea);
            paintExtraLeftPanel(g, extraArea);
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
            g.setColour(juce::Colour(0xff888888));  // Grey
            g.drawRoundedRectangle(bounds.toFloat().reduced(1.0f), 4.0f, 2.0f);
        }
        return;
    }

    // === LEFT SIDE PANELS: [Macros][MacroEditor][Mods][ModEditor] (squared corners) ===
    if (paramPanelVisible_) {
        auto paramArea = bounds.removeFromLeft(getParamPanelWidth());
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
        g.fillRect(paramArea);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRect(paramArea);
        paintParamPanel(g, paramArea);
    }

    // Macro editor panel - after macros, before mods
    int extraRightWidth = getExtraRightPanelWidth();
    if (extraRightWidth > 0) {
        auto extraRightArea = bounds.removeFromLeft(extraRightWidth);
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
        g.fillRect(extraRightArea);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRect(extraRightArea);
        paintExtraRightPanel(g, extraRightArea);
    }

    if (modPanelVisible_) {
        auto modArea = bounds.removeFromLeft(getModPanelWidth());
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
        g.fillRect(modArea);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRect(modArea);
        paintModPanel(g, modArea);
    }

    // Mod editor panel - after mods, before main content
    int extraWidth = getExtraLeftPanelWidth();
    if (extraWidth > 0) {
        auto extraArea = bounds.removeFromLeft(extraWidth);
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
        g.fillRect(extraArea);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRect(extraArea);
        paintExtraLeftPanel(g, extraArea);
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

    // Calculate content area (below header)
    auto contentArea = bounds;
    contentArea.removeFromTop(headerHeight);

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
        // === LEFT SIDE PANELS (even when collapsed): [Macros][MacroEditor][Mods][ModEditor] ===
        if (paramPanelVisible_) {
            auto paramArea = bounds.removeFromLeft(getParamPanelWidth());
            resizedParamPanel(paramArea);
        } else {
            // Hide param knobs when panel is not visible
            for (auto& knob : paramKnobs_) {
                knob->setVisible(false);
            }
            // Hide the real macro panel if it exists
            if (macroPanel_)
                macroPanel_->setVisible(false);
        }

        // Macro editor panel - after macros, before mods
        int extraRightWidthCollapsed = getExtraRightPanelWidth();
        if (extraRightWidthCollapsed > 0) {
            auto extraRightArea = bounds.removeFromLeft(extraRightWidthCollapsed);
            resizedExtraRightPanel(extraRightArea);
        }

        if (modPanelVisible_) {
            auto modArea = bounds.removeFromLeft(getModPanelWidth());
            resizedModPanel(modArea);
        } else {
            // Hide mod slot buttons when panel is not visible
            for (auto& btn : modSlotButtons_) {
                if (btn)
                    btn->setVisible(false);
            }
            // Hide the real mods panel if it exists
            if (modsPanel_)
                modsPanel_->setVisible(false);
        }

        // Mod editor panel - after mods, before main content
        int extraWidthCollapsed = getExtraLeftPanelWidth();
        if (extraWidthCollapsed > 0) {
            auto extraArea = bounds.removeFromLeft(extraWidthCollapsed);
            resizedExtraLeftPanel(extraArea);
        }

        // === RIGHT SIDE PANEL (even when collapsed) ===
        if (gainPanelVisible_) {
            auto gainArea = bounds.removeFromRight(getGainPanelWidth());
            resizedGainPanel(gainArea);
        }

        // === COLLAPSED MAIN STRIP (remaining bounds) ===
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

    // === LEFT SIDE PANELS: [Macros][MacroEditor][Mods][ModEditor] ===
    if (paramPanelVisible_) {
        auto paramArea = bounds.removeFromLeft(getParamPanelWidth());
        resizedParamPanel(paramArea);
    } else {
        // Hide param knobs when panel is not visible
        for (auto& knob : paramKnobs_) {
            knob->setVisible(false);
        }
        // Hide the real macro panel if it exists
        if (macroPanel_)
            macroPanel_->setVisible(false);
    }

    // Macro editor panel - after macros, before mods
    int extraRightWidth = getExtraRightPanelWidth();
    if (extraRightWidth > 0) {
        auto extraRightArea = bounds.removeFromLeft(extraRightWidth);
        resizedExtraRightPanel(extraRightArea);
    }

    if (modPanelVisible_) {
        auto modArea = bounds.removeFromLeft(getModPanelWidth());
        resizedModPanel(modArea);
    } else {
        // Hide mod slot buttons when panel is not visible
        for (auto& btn : modSlotButtons_) {
            if (btn)
                btn->setVisible(false);
        }
        // Hide the real mods panel if it exists
        if (modsPanel_)
            modsPanel_->setVisible(false);
    }

    // Mod editor panel - after mods, before main content
    int extraWidth = getExtraLeftPanelWidth();
    if (extraWidth > 0) {
        auto extraArea = bounds.removeFromLeft(extraWidth);
        resizedExtraLeftPanel(extraArea);
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

    // === CONTENT (remaining area) ===
    // Reduce by 2 horizontally, 1 vertically to keep border visible
    auto contentArea = bounds.reduced(2, 1);
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

void NodeComponent::setModPanelVisible(bool visible) {
    if (modPanelVisible_ != visible) {
        modPanelVisible_ = visible;

        // When hiding the mod panel, also hide the modulator editor
        if (!visible && modulatorEditorVisible_) {
            hideModulatorEditor();
        }

        if (onModPanelToggled) {
            onModPanelToggled(modPanelVisible_);
        }
        resized();
        repaint();
        if (onLayoutChanged) {
            onLayoutChanged();
        }
    }
}

void NodeComponent::setParamPanelVisible(bool visible) {
    if (paramPanelVisible_ != visible) {
        DBG("NodeComponent::setParamPanelVisible - changing from "
            << (paramPanelVisible_ ? "visible" : "hidden") << " to "
            << (visible ? "visible" : "hidden"));
        paramPanelVisible_ = visible;

        // When hiding the macro panel, also hide the macro editor
        if (!visible && macroEditorVisible_) {
            hideMacroEditor();
        }

        if (onParamPanelToggled) {
            onParamPanelToggled(paramPanelVisible_);
        }
        resized();
        repaint();
        if (onLayoutChanged) {
            onLayoutChanged();
        }
    }
}

void NodeComponent::setGainPanelVisible(bool visible) {
    if (gainPanelVisible_ != visible) {
        gainPanelVisible_ = visible;
        if (onGainPanelToggled) {
            onGainPanelToggled(gainPanelVisible_);
        }
        resized();
        repaint();
        if (onLayoutChanged) {
            onLayoutChanged();
        }
    }
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
    width += getExtraLeftPanelWidth();  // Extra left panel (e.g., mod editor)
    if (paramPanelVisible_)
        width += getParamPanelWidth();
    width += getExtraRightPanelWidth();  // Extra "right" panel (e.g., macro editor) - still left of
                                         // main content
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

int NodeComponent::getExtraLeftPanelWidth() const {
    return getModulatorEditorWidth();
}

int NodeComponent::getExtraRightPanelWidth() const {
    return getMacroEditorWidth();
}

void NodeComponent::paintModPanel(juce::Graphics& g, juce::Rectangle<int> panelArea) {
    // If we have a real mods panel, just draw the header
    if (modsPanel_) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
        g.setFont(FontManager::getInstance().getUIFontBold(9.0f));
        g.drawText("MODS", panelArea.removeFromTop(16), juce::Justification::centred);
        return;
    }
    // Default: draw labeled placeholder (vertical side panel)
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    g.setFont(FontManager::getInstance().getUIFont(8.0f));
    g.drawText("MOD", panelArea.removeFromTop(16), juce::Justification::centred);
}

void NodeComponent::paintExtraLeftPanel(juce::Graphics& g, juce::Rectangle<int> panelArea) {
    // Draw modulator editor panel header if visible
    if (modulatorEditorVisible_ && modulatorEditorPanel_) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).darker(0.2f));
        g.setFont(FontManager::getInstance().getUIFontBold(9.0f));
        g.drawText("MOD EDIT", panelArea.removeFromTop(16), juce::Justification::centred);
    }
}

void NodeComponent::paintParamPanel(juce::Graphics& g, juce::Rectangle<int> panelArea) {
    // If we have a real macros panel, just draw the header
    if (macroPanel_) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
        g.setFont(FontManager::getInstance().getUIFontBold(9.0f));
        g.drawText("MACROS", panelArea.removeFromTop(16), juce::Justification::centred);
        return;
    }
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
    panelArea.removeFromTop(16);  // Skip header

    // If we have a real mods panel, use it
    if (modsPanel_) {
        modsPanel_->setBounds(panelArea);
        modsPanel_->setVisible(true);
        updateModsPanel();
        // Hide placeholder buttons
        for (auto& btn : modSlotButtons_) {
            if (btn)
                btn->setVisible(false);
        }
        return;
    }

    // Default: placeholder mod slot buttons
    panelArea = panelArea.reduced(2);
    int slotHeight = (panelArea.getHeight() - 4) / 3;
    for (int i = 0; i < 3; ++i) {
        modSlotButtons_[i]->setBounds(panelArea.removeFromTop(slotHeight).reduced(0, 1));
        modSlotButtons_[i]->setVisible(true);
    }
}

void NodeComponent::resizedExtraLeftPanel(juce::Rectangle<int> panelArea) {
    // Layout modulator editor panel if visible
    if (modulatorEditorVisible_ && modulatorEditorPanel_) {
        panelArea.removeFromTop(16);  // Skip header
        modulatorEditorPanel_->setBounds(panelArea);
        modulatorEditorPanel_->setVisible(true);
    } else if (modulatorEditorPanel_) {
        modulatorEditorPanel_->setVisible(false);
    }
}

void NodeComponent::resizedParamPanel(juce::Rectangle<int> panelArea) {
    panelArea.removeFromTop(16);  // Skip header

    // If we have a real macros panel, use it
    if (macroPanel_) {
        macroPanel_->setBounds(panelArea);
        macroPanel_->setVisible(true);
        updateMacroPanel();
        // Hide placeholder knobs
        for (auto& knob : paramKnobs_) {
            knob->setVisible(false);
        }
        return;
    }

    // Default: placeholder param knobs
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

void NodeComponent::paintExtraRightPanel(juce::Graphics& g, juce::Rectangle<int> panelArea) {
    // Draw macro editor panel header if visible
    if (macroEditorVisible_ && macroEditorPanel_) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).darker(0.2f));
        g.setFont(FontManager::getInstance().getUIFontBold(9.0f));
        g.drawText("MACRO EDIT", panelArea.removeFromTop(16), juce::Justification::centred);
    }
}

void NodeComponent::resizedExtraRightPanel(juce::Rectangle<int> panelArea) {
    // Layout macro editor panel if visible
    if (macroEditorVisible_ && macroEditorPanel_) {
        panelArea.removeFromTop(16);  // Skip header
        macroEditorPanel_->setBounds(panelArea);
        macroEditorPanel_->setVisible(true);
    } else if (macroEditorPanel_) {
        macroEditorPanel_->setVisible(false);
    }
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

    // Update mods/macros panels with parent path for drag-and-drop
    if (modsPanel_) {
        modsPanel_->setParentPath(path);
    }
    if (macroPanel_) {
        macroPanel_->setParentPath(path);
    }
}

void NodeComponent::selectionTypeChanged(magda::SelectionType newType) {
    // If selection type changed away from ChainNode, deselect this node
    if (newType != magda::SelectionType::ChainNode) {
        setSelected(false);
    }
}

void NodeComponent::chainNodeSelectionChanged(const magda::ChainNodePath& path) {
    // Update our selection state based on whether we match the selected path
    bool shouldBeSelected = nodePath_.isValid() && nodePath_ == path;
    setSelected(shouldBeSelected);
}

void NodeComponent::chainNodeReselected(const magda::ChainNodePath& /*path*/) {
    // Not used - we handle collapse toggle directly in mouseUp
}

void NodeComponent::paramSelectionChanged(const magda::ParamSelection& selection) {}

void NodeComponent::mouseDown(const juce::MouseEvent& e) {
    // Only handle left clicks for selection
    if (e.mods.isLeftButtonDown()) {
        mouseDownForSelection_ = true;

        // Capture drag start position in parent coordinates
        if (draggable_) {
            if (auto* parent = getParentComponent()) {
                dragStartPos_ = e.getEventRelativeTo(parent).getPosition();
            }
            dragStartBounds_ = getBounds().getPosition();
            isDragging_ = false;
        }
    }
}

void NodeComponent::mouseDrag(const juce::MouseEvent& e) {
    if (!mouseDownForSelection_ || !draggable_)
        return;

    auto* parent = getParentComponent();
    if (!parent)
        return;

    auto currentPos = e.getEventRelativeTo(parent).getPosition();
    int deltaX = std::abs(currentPos.x - dragStartPos_.x);

    // Check threshold before starting drag
    if (!isDragging_ && deltaX > DRAG_THRESHOLD) {
        isDragging_ = true;
        if (onDragStart)
            onDragStart(this, e);
    }

    if (isDragging_ && onDragMove) {
        onDragMove(this, e);
    }
}

void NodeComponent::mouseUp(const juce::MouseEvent& e) {
    // If we were dragging, commit the drag and skip selection
    if (isDragging_) {
        if (onDragEnd)
            onDragEnd(this, e);
        isDragging_ = false;
        mouseDownForSelection_ = false;
        return;
    }

    // Complete selection on mouse up (click-and-release) - only if not dragging
    if (mouseDownForSelection_ && !e.mods.isPopupMenu()) {
        mouseDownForSelection_ = false;

        // Check if mouse is still within bounds (not a drag-away)
        if (getLocalBounds().contains(e.getPosition())) {
            if (nodePath_.isValid()) {
                // Capture state BEFORE calling selectChainNode
                // (callbacks may change these values synchronously)
                bool wasAlreadySelected = selected_;
                bool wasCollapsed = collapsed_;

                magda::SelectionManager::getInstance().selectChainNode(nodePath_);

                // If was already selected, toggle collapse using captured state
                if (wasAlreadySelected) {
                    setCollapsed(!wasCollapsed);
                }
            }

            // Also call legacy callback for backward compatibility
            if (onSelected) {
                onSelected();
            }
        }
    }

    isDragging_ = false;
}

void NodeComponent::mouseWheelMove(const juce::MouseEvent& e,
                                   const juce::MouseWheelDetails& wheel) {
    DBG("NodeComponent::mouseWheelMove - deltaY=" << wheel.deltaY << " isCommandDown="
                                                  << (e.mods.isCommandDown() ? "yes" : "no"));

    // Cmd/Ctrl + scroll wheel = zoom (forward to parent chain panel)
    if (e.mods.isCommandDown() && onZoomDelta) {
        float delta = wheel.deltaY > 0 ? 0.1f : -0.1f;
        onZoomDelta(delta);
    } else {
        // Let parent handle normal scrolling
        Component::mouseWheelMove(e, wheel);
    }
}

// === Mods/Macros Panel Support ===

void NodeComponent::initializeModsMacrosPanels() {
    // Create mods panel
    modsPanel_ = std::make_unique<ModsPanelComponent>();
    modsPanel_->onModAmountChanged = [this](int modIndex, float amount) {
        onModAmountChangedInternal(modIndex, amount);
    };
    modsPanel_->onModTargetChanged = [this](int modIndex, magda::ModTarget target) {
        onModTargetChangedInternal(modIndex, target);
    };
    modsPanel_->onModNameChanged = [this](int modIndex, juce::String name) {
        onModNameChangedInternal(modIndex, name);
    };
    modsPanel_->onModClicked = [this](int modIndex) {
        onModClickedInternal(modIndex);
        // Toggle modulator editor - if clicking same mod, hide; otherwise show
        if (modulatorEditorVisible_ && selectedModIndex_ == modIndex) {
            hideModulatorEditor();
        } else {
            showModulatorEditor(modIndex);
        }
    };
    modsPanel_->onAddModRequested = [this](int slotIndex, magda::ModType type,
                                           magda::LFOWaveform waveform) {
        onAddModRequestedInternal(slotIndex, type, waveform);
    };
    modsPanel_->onModRemoveRequested = [this](int modIndex) {
        onModRemoveRequestedInternal(modIndex);
    };
    modsPanel_->onModEnableToggled = [this](int modIndex, bool enabled) {
        onModEnableToggledInternal(modIndex, enabled);
    };
    modsPanel_->onAddPageRequested = [this](int itemsToAdd) { onModPageAddRequested(itemsToAdd); };
    modsPanel_->onRemovePageRequested = [this](int itemsToRemove) {
        onModPageRemoveRequested(itemsToRemove);
    };
    modsPanel_->onPanelClicked = [this]() {
        magda::SelectionManager::getInstance().selectModsPanel(nodePath_);
    };
    addChildComponent(*modsPanel_);

    // Create macro panel
    macroPanel_ = std::make_unique<MacroPanelComponent>();
    macroPanel_->onMacroValueChanged = [this](int macroIndex, float value) {
        onMacroValueChangedInternal(macroIndex, value);
    };
    macroPanel_->onMacroTargetChanged = [this](int macroIndex, magda::MacroTarget target) {
        onMacroTargetChangedInternal(macroIndex, target);
    };
    macroPanel_->onMacroNameChanged = [this](int macroIndex, juce::String name) {
        onMacroNameChangedInternal(macroIndex, name);
    };
    macroPanel_->onMacroClicked = [this](int macroIndex) {
        onMacroClickedInternal(macroIndex);
        // Toggle macro editor - if clicking same macro, hide; otherwise show
        if (macroEditorVisible_ && selectedMacroIndex_ == macroIndex) {
            hideMacroEditor();
        } else {
            showMacroEditor(macroIndex);
        }
    };
    macroPanel_->onAddPageRequested = [this](int itemsToAdd) {
        onMacroPageAddRequested(itemsToAdd);
    };
    macroPanel_->onRemovePageRequested = [this](int itemsToRemove) {
        onMacroPageRemoveRequested(itemsToRemove);
    };
    macroPanel_->onPanelClicked = [this]() {
        magda::SelectionManager::getInstance().selectMacrosPanel(nodePath_);
    };
    addChildComponent(*macroPanel_);

    // Create modulator editor panel
    modulatorEditorPanel_ = std::make_unique<ModulatorEditorPanel>();
    modulatorEditorPanel_->onRateChanged = [this](float rate) {
        if (selectedModIndex_ >= 0) {
            onModRateChangedInternal(selectedModIndex_, rate);
        }
    };
    modulatorEditorPanel_->onWaveformChanged = [this](magda::LFOWaveform waveform) {
        if (selectedModIndex_ >= 0) {
            onModWaveformChangedInternal(selectedModIndex_, waveform);
        }
    };
    modulatorEditorPanel_->onTempoSyncChanged = [this](bool tempoSync) {
        if (selectedModIndex_ >= 0) {
            onModTempoSyncChangedInternal(selectedModIndex_, tempoSync);
        }
    };
    modulatorEditorPanel_->onSyncDivisionChanged = [this](magda::SyncDivision division) {
        if (selectedModIndex_ >= 0) {
            onModSyncDivisionChangedInternal(selectedModIndex_, division);
        }
    };
    modulatorEditorPanel_->onTriggerModeChanged = [this](magda::LFOTriggerMode mode) {
        if (selectedModIndex_ >= 0) {
            onModTriggerModeChangedInternal(selectedModIndex_, mode);
        }
    };
    modulatorEditorPanel_->onCurvePresetChanged = [this](magda::CurvePreset preset) {
        if (selectedModIndex_ >= 0) {
            onModCurvePresetChangedInternal(selectedModIndex_, preset);
        }
    };
    modulatorEditorPanel_->onOpenCurveEditor = [this]() {
        if (selectedModIndex_ >= 0) {
            onOpenCurveEditorInternal(selectedModIndex_);
        }
    };
    addChildComponent(*modulatorEditorPanel_);

    // Create macro editor panel
    macroEditorPanel_ = std::make_unique<MacroEditorPanel>();
    macroEditorPanel_->onNameChanged = [this](juce::String name) {
        if (selectedMacroIndex_ >= 0) {
            onMacroNameChangedInternal(selectedMacroIndex_, name);
        }
    };
    macroEditorPanel_->onValueChanged = [this](float value) {
        if (selectedMacroIndex_ >= 0) {
            onMacroValueChangedInternal(selectedMacroIndex_, value);
        }
    };
    addChildComponent(*macroEditorPanel_);
}

void NodeComponent::updateModsPanel() {
    if (!modsPanel_)
        return;

    const auto* mods = getModsData();
    if (mods) {
        modsPanel_->setMods(*mods);
    }

    auto devices = getAvailableDevices();
    modsPanel_->setAvailableDevices(devices);
}

void NodeComponent::updateMacroPanel() {
    if (!macroPanel_)
        return;

    const auto* macros = getMacrosData();
    if (macros) {
        macroPanel_->setMacros(*macros);
    }

    auto devices = getAvailableDevices();
    macroPanel_->setAvailableDevices(devices);
}

// === Modulator Editor Panel ===

void NodeComponent::showModulatorEditor(int modIndex) {
    selectedModIndex_ = modIndex;
    modulatorEditorVisible_ = true;

    updateModulatorEditor();

    // Trigger relayout
    resized();
    repaint();
    if (onLayoutChanged)
        onLayoutChanged();
}

void NodeComponent::hideModulatorEditor() {
    selectedModIndex_ = -1;
    modulatorEditorVisible_ = false;

    if (modulatorEditorPanel_) {
        modulatorEditorPanel_->setVisible(false);
        modulatorEditorPanel_->setSelectedModIndex(-1);
    }

    // Trigger relayout
    resized();
    repaint();
    if (onLayoutChanged)
        onLayoutChanged();
}

void NodeComponent::updateModulatorEditor() {
    if (!modulatorEditorPanel_ || selectedModIndex_ < 0)
        return;

    const auto* mods = getModsData();
    if (!mods)
        return;

    if (selectedModIndex_ < static_cast<int>(mods->size())) {
        // Pass pointer to live mod for animated waveform display
        modulatorEditorPanel_->setModInfo((*mods)[selectedModIndex_], &(*mods)[selectedModIndex_]);
        modulatorEditorPanel_->setSelectedModIndex(selectedModIndex_);
    }
}

// === Macro Editor Panel ===

void NodeComponent::showMacroEditor(int macroIndex) {
    selectedMacroIndex_ = macroIndex;
    macroEditorVisible_ = true;

    updateMacroEditor();

    // Trigger relayout
    resized();
    repaint();
    if (onLayoutChanged)
        onLayoutChanged();
}

void NodeComponent::hideMacroEditor() {
    selectedMacroIndex_ = -1;
    macroEditorVisible_ = false;

    if (macroEditorPanel_) {
        macroEditorPanel_->setVisible(false);
        macroEditorPanel_->setSelectedMacroIndex(-1);
    }

    // Trigger relayout
    resized();
    repaint();
    if (onLayoutChanged)
        onLayoutChanged();
}

void NodeComponent::updateMacroEditor() {
    if (!macroEditorPanel_ || selectedMacroIndex_ < 0)
        return;

    const auto* macros = getMacrosData();
    if (!macros)
        return;

    if (selectedMacroIndex_ < static_cast<int>(macros->size())) {
        macroEditorPanel_->setMacroInfo((*macros)[selectedMacroIndex_]);
        macroEditorPanel_->setSelectedMacroIndex(selectedMacroIndex_);
    }
}

int NodeComponent::getModulatorEditorWidth() const {
    return modulatorEditorVisible_ ? ModulatorEditorPanel::PREFERRED_WIDTH : 0;
}

int NodeComponent::getMacroEditorWidth() const {
    return macroEditorVisible_ ? MacroEditorPanel::PREFERRED_WIDTH : 0;
}

}  // namespace magda::daw::ui
