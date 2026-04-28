#include "DarkTheme.hpp"

namespace magda {

void DarkTheme::applyToLookAndFeel(juce::LookAndFeel_V4& laf) {
    // V4 ColourScheme drives the title bar background (widgetBackground) and a
    // few other top-level surfaces that the colour-ID system doesn't reach.
    laf.setColourScheme({
        getColour(BACKGROUND),        // windowBackground
        getColour(BACKGROUND),        // widgetBackground (DocumentWindow title bar)
        getColour(PANEL_BACKGROUND),  // menuBackground
        getColour(BORDER),            // outline
        getColour(TEXT_PRIMARY),      // defaultText
        getColour(BUTTON_NORMAL),     // defaultFill
        getColour(TEXT_PRIMARY),      // highlightedText
        getColour(ACCENT_BLUE),       // highlightedFill
        getColour(TEXT_PRIMARY),      // menuText
    });

    // Background colors
    laf.setColour(juce::ResizableWindow::backgroundColourId, getColour(BACKGROUND));
    laf.setColour(juce::DocumentWindow::backgroundColourId, getColour(BACKGROUND));
    laf.setColour(juce::DocumentWindow::textColourId, getColour(TEXT_PRIMARY));

    // Text colors
    laf.setColour(juce::Label::textColourId, getColour(TEXT_PRIMARY));
    laf.setColour(juce::TextEditor::textColourId, getColour(TEXT_PRIMARY));
    laf.setColour(juce::TextEditor::backgroundColourId, getColour(SURFACE));
    laf.setColour(juce::TextEditor::outlineColourId, getColour(BORDER));
    laf.setColour(juce::TextEditor::focusedOutlineColourId, getColour(ACCENT_BLUE));
    laf.setColour(juce::CaretComponent::caretColourId, getColour(TEXT_PRIMARY));

    // Button colors
    laf.setColour(juce::TextButton::buttonColourId, getColour(BUTTON_NORMAL));
    laf.setColour(juce::TextButton::buttonOnColourId, getColour(BUTTON_ACTIVE));
    laf.setColour(juce::TextButton::textColourOffId, getColour(TEXT_PRIMARY));
    laf.setColour(juce::TextButton::textColourOnId, getColour(TEXT_PRIMARY));

    // Toggle button colors
    laf.setColour(juce::ToggleButton::textColourId, getColour(TEXT_PRIMARY));
    laf.setColour(juce::ToggleButton::tickColourId, getColour(ACCENT_BLUE));
    laf.setColour(juce::ToggleButton::tickDisabledColourId, getColour(TEXT_DISABLED));

    // Slider colors
    laf.setColour(juce::Slider::backgroundColourId, getColour(SURFACE));
    laf.setColour(juce::Slider::thumbColourId, getColour(ACCENT_BLUE));
    laf.setColour(juce::Slider::trackColourId, getColour(BUTTON_NORMAL));
    laf.setColour(juce::Slider::rotarySliderFillColourId, getColour(ACCENT_BLUE));
    laf.setColour(juce::Slider::rotarySliderOutlineColourId, getColour(BORDER));
    laf.setColour(juce::Slider::textBoxTextColourId, getColour(TEXT_PRIMARY));
    laf.setColour(juce::Slider::textBoxBackgroundColourId, getColour(SURFACE));
    laf.setColour(juce::Slider::textBoxOutlineColourId, getColour(BORDER));

    // ComboBox colors
    laf.setColour(juce::ComboBox::backgroundColourId, getColour(SURFACE));
    laf.setColour(juce::ComboBox::textColourId, getColour(TEXT_PRIMARY));
    laf.setColour(juce::ComboBox::outlineColourId, getColour(BORDER));
    laf.setColour(juce::ComboBox::arrowColourId, getColour(TEXT_SECONDARY));
    laf.setColour(juce::ComboBox::buttonColourId, getColour(BUTTON_NORMAL));

    // PopupMenu colors
    laf.setColour(juce::PopupMenu::backgroundColourId, getColour(PANEL_BACKGROUND));
    laf.setColour(juce::PopupMenu::textColourId, getColour(TEXT_PRIMARY));
    laf.setColour(juce::PopupMenu::headerTextColourId, getColour(TEXT_SECONDARY));
    laf.setColour(juce::PopupMenu::highlightedBackgroundColourId, getColour(SURFACE_HOVER));
    laf.setColour(juce::PopupMenu::highlightedTextColourId, getColour(TEXT_PRIMARY));

    // Scrollbar colors
    laf.setColour(juce::ScrollBar::backgroundColourId, getColour(PANEL_BACKGROUND));
    laf.setColour(juce::ScrollBar::thumbColourId, getColour(BUTTON_NORMAL));
    laf.setColour(juce::ScrollBar::trackColourId, getColour(SURFACE));

    // TreeView colors
    laf.setColour(juce::TreeView::backgroundColourId, getColour(PANEL_BACKGROUND));
    laf.setColour(juce::TreeView::linesColourId, getColour(BORDER));
    laf.setColour(juce::TreeView::dragAndDropIndicatorColourId, getColour(ACCENT_BLUE));
    laf.setColour(juce::TreeView::selectedItemBackgroundColourId, getColour(SURFACE_HOVER));

    // ListBox colors
    laf.setColour(juce::ListBox::backgroundColourId, getColour(PANEL_BACKGROUND));
    laf.setColour(juce::ListBox::outlineColourId, getColour(BORDER));
    laf.setColour(juce::ListBox::textColourId, getColour(TEXT_PRIMARY));

    // Toolbar colors
    laf.setColour(juce::Toolbar::backgroundColourId, getColour(TRANSPORT_BACKGROUND));
    laf.setColour(juce::Toolbar::separatorColourId, getColour(SEPARATOR));
    laf.setColour(juce::Toolbar::buttonMouseOverBackgroundColourId, getColour(BUTTON_HOVER));
    laf.setColour(juce::Toolbar::buttonMouseDownBackgroundColourId, getColour(BUTTON_PRESSED));

    // AlertWindow colors
    laf.setColour(juce::AlertWindow::backgroundColourId, getColour(PANEL_BACKGROUND));
    laf.setColour(juce::AlertWindow::textColourId, getColour(TEXT_PRIMARY));
    laf.setColour(juce::AlertWindow::outlineColourId, getColour(BORDER));

    // TabbedComponent colors
    laf.setColour(juce::TabbedComponent::backgroundColourId, getColour(PANEL_BACKGROUND));
    laf.setColour(juce::TabbedComponent::outlineColourId, getColour(BORDER));
    laf.setColour(juce::TabbedButtonBar::tabOutlineColourId, getColour(BORDER));
    laf.setColour(juce::TabbedButtonBar::tabTextColourId, getColour(TEXT_SECONDARY));
    laf.setColour(juce::TabbedButtonBar::frontOutlineColourId, getColour(ACCENT_BLUE));
    laf.setColour(juce::TabbedButtonBar::frontTextColourId, getColour(TEXT_PRIMARY));

    // PropertyPanel colors
    laf.setColour(juce::PropertyComponent::backgroundColourId, getColour(PANEL_BACKGROUND));
    laf.setColour(juce::PropertyComponent::labelTextColourId, getColour(TEXT_PRIMARY));

    // Note: CodeEditorComponent colors removed as they require additional JUCE modules
}

}  // namespace magda
