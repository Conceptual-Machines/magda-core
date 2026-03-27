#include "MacroEditorPanel.hpp"

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

// ============================================================================
// MacroLinkMatrixContent
// ============================================================================

void MacroLinkMatrixContent::setLinks(const std::vector<LinkRow>& links) {
    links_ = links;
    setSize(getWidth(), juce::jmax(1, static_cast<int>(links_.size())) * ROW_HEIGHT);
    repaint();
}

void MacroLinkMatrixContent::paint(juce::Graphics& g) {
    auto font = FontManager::getInstance().getUIFont(8.0f);
    g.setFont(font);

    for (int i = 0; i < static_cast<int>(links_.size()); ++i) {
        const auto& link = links_[static_cast<size_t>(i)];
        int y = i * ROW_HEIGHT;
        auto rowBounds = juce::Rectangle<int>(0, y, getWidth(), ROW_HEIGHT);

        // Alternating row background
        if (i % 2 == 0) {
            g.setColour(DarkTheme::getColour(DarkTheme::SURFACE).withAlpha(0.3f));
            g.fillRect(rowBounds);
        }

        auto remaining = rowBounds.reduced(2, 0);

        // Delete button (x) on right - 14px
        auto deleteBounds = remaining.removeFromRight(14);
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.drawText("x", deleteBounds, juce::Justification::centred);
        remaining.removeFromRight(2);

        // Amount - 28px
        auto amountBounds = remaining.removeFromRight(28);
        int percent = static_cast<int>(link.amount * 100.0f);
        g.setColour(DarkTheme::getTextColour());
        g.drawText(juce::String(percent) + "%", amountBounds, juce::Justification::centredRight);
        remaining.removeFromRight(2);

        // Param name takes remaining space
        g.setColour(DarkTheme::getTextColour());
        g.drawText(link.paramName, remaining, juce::Justification::centredLeft, true);
    }

    if (links_.empty()) {
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.drawText("No links", getLocalBounds(), juce::Justification::centred);
    }
}

void MacroLinkMatrixContent::mouseDown(const juce::MouseEvent& e) {
    int rowIndex = e.getPosition().y / ROW_HEIGHT;
    if (rowIndex < 0 || rowIndex >= static_cast<int>(links_.size()))
        return;

    int x = e.getPosition().x;
    int width = getWidth();

    // Delete button zone: rightmost 14px + 2px padding
    if (x >= width - 16) {
        if (onDeleteLink)
            onDeleteLink(links_[static_cast<size_t>(rowIndex)].target);
        return;
    }

    // Amount drag — anywhere else in the row
    draggingRow_ = rowIndex;
    dragStartAmount_ = links_[static_cast<size_t>(rowIndex)].amount;
    dragStartX_ = e.getPosition().x;
}

void MacroLinkMatrixContent::mouseDrag(const juce::MouseEvent& e) {
    if (draggingRow_ < 0 || draggingRow_ >= static_cast<int>(links_.size()))
        return;

    float delta = static_cast<float>(e.getPosition().x - dragStartX_) / 100.0f;
    float newAmount = juce::jlimit(0.0f, 1.0f, dragStartAmount_ + delta);
    links_[static_cast<size_t>(draggingRow_)].amount = newAmount;
    repaint();

    if (onAmountChanged)
        onAmountChanged(links_[static_cast<size_t>(draggingRow_)].target, newAmount);
}

void MacroLinkMatrixContent::mouseUp(const juce::MouseEvent&) {
    draggingRow_ = -1;
}

// ============================================================================
// MacroEditorPanel
// ============================================================================

MacroEditorPanel::MacroEditorPanel() {
    // Intercept mouse clicks to prevent propagation to parent
    setInterceptsMouseClicks(true, true);

    // Name label at top (editable)
    nameLabel_.setFont(FontManager::getInstance().getUIFontBold(10.0f));
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    nameLabel_.setJustificationType(juce::Justification::centred);
    nameLabel_.setText("No Macro Selected", juce::dontSendNotification);
    nameLabel_.setEditable(false, true);  // Single-click doesn't edit, double-click does
    nameLabel_.onTextChange = [this]() {
        if (onNameChanged) {
            onNameChanged(nameLabel_.getText());
        }
    };
    addAndMakeVisible(nameLabel_);

    // Value slider
    valueSlider_.setRange(0.0, 1.0, 0.01);
    valueSlider_.setValue(0.5, juce::dontSendNotification);
    valueSlider_.setFont(FontManager::getInstance().getUIFont(9.0f));
    valueSlider_.onValueChanged = [this](double value) {
        currentMacro_.value = static_cast<float>(value);
        if (onValueChanged) {
            onValueChanged(currentMacro_.value);
        }
    };
    addAndMakeVisible(valueSlider_);

    // Link matrix viewport + content
    linkMatrixContent_.onDeleteLink = [this](magda::MacroTarget target) {
        if (onLinkRemoved)
            onLinkRemoved(target);
    };
    linkMatrixContent_.onAmountChanged = [this](magda::MacroTarget target, float amount) {
        if (onLinkAmountChanged)
            onLinkAmountChanged(target, amount);
    };
    linkMatrixViewport_.setViewedComponent(&linkMatrixContent_, false);
    linkMatrixViewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(linkMatrixViewport_);
}

void MacroEditorPanel::setMacroInfo(const magda::MacroInfo& macro) {
    currentMacro_ = macro;
    updateFromMacro();
}

void MacroEditorPanel::setSelectedMacroIndex(int index) {
    selectedMacroIndex_ = index;
    if (index < 0) {
        nameLabel_.setText("No Macro Selected", juce::dontSendNotification);
        nameLabel_.setEditable(false, false);
        valueSlider_.setEnabled(false);
        linkMatrixContent_.setLinks({});
    } else {
        nameLabel_.setEditable(false, true);  // Allow double-click editing
        valueSlider_.setEnabled(true);
    }
}

void MacroEditorPanel::updateFromMacro() {
    nameLabel_.setText(currentMacro_.name, juce::dontSendNotification);
    valueSlider_.setValue(currentMacro_.value, juce::dontSendNotification);

    std::vector<MacroLinkMatrixContent::LinkRow> rows;

    // Prefer links vector (new multi-link system)
    for (const auto& link : currentMacro_.links) {
        if (!link.target.isValid())
            continue;
        MacroLinkMatrixContent::LinkRow row;
        row.target = link.target;
        row.amount = link.amount;
        if (paramNameResolver_) {
            row.paramName = paramNameResolver_(link.target.deviceId, link.target.paramIndex);
        } else {
            row.paramName = "Device " + juce::String(link.target.deviceId) + " P" +
                            juce::String(link.target.paramIndex + 1);
        }
        rows.push_back(row);
    }

    // Legacy fallback: single target
    if (rows.empty() && currentMacro_.target.isValid()) {
        MacroLinkMatrixContent::LinkRow row;
        row.target = currentMacro_.target;
        row.amount = 1.0f;
        if (paramNameResolver_) {
            row.paramName =
                paramNameResolver_(currentMacro_.target.deviceId, currentMacro_.target.paramIndex);
        } else {
            row.paramName = "Device " + juce::String(currentMacro_.target.deviceId) + " P" +
                            juce::String(currentMacro_.target.paramIndex + 1);
        }
        rows.push_back(row);
    }

    linkMatrixContent_.setLinks(rows);
    resized();
}

void MacroEditorPanel::paint(juce::Graphics& g) {
    // Background
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.03f));
    g.fillRect(getLocalBounds());

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds());

    // Section headers
    auto bounds = getLocalBounds().reduced(4);
    bounds.removeFromTop(24);  // Skip name label

    // "Value" label
    g.setColour(DarkTheme::getSecondaryTextColour());
    g.setFont(FontManager::getInstance().getUIFont(8.0f));
    g.drawText("Value", bounds.removeFromTop(12), juce::Justification::centredLeft);
    bounds.removeFromTop(20);  // Skip value slider
    bounds.removeFromTop(8);

    // "Links" section header
    g.drawText("Links", bounds.removeFromTop(12), juce::Justification::centredLeft);
}

void MacroEditorPanel::resized() {
    auto bounds = getLocalBounds().reduced(4);

    // Name label at top
    nameLabel_.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(4);

    // Value label area (painted) + slider
    bounds.removeFromTop(12);  // "Value" label
    valueSlider_.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(8);

    // Links section header (painted) + matrix
    bounds.removeFromTop(12);  // "Links" label

    // Link matrix takes remaining space
    if (bounds.getHeight() > 0) {
        linkMatrixViewport_.setBounds(bounds);
        linkMatrixContent_.setSize(bounds.getWidth(), linkMatrixContent_.getHeight());
    }
}

void MacroEditorPanel::mouseDown(const juce::MouseEvent& /*e*/) {
    // Consume mouse events to prevent propagation to parent
}

void MacroEditorPanel::mouseUp(const juce::MouseEvent& /*e*/) {
    // Consume mouse events to prevent propagation to parent
}

}  // namespace magda::daw::ui
