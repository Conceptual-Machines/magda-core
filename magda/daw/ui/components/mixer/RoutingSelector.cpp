#include "RoutingSelector.hpp"

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda {

RoutingSelector::RoutingSelector(Type type) : type_(type) {
    setRepaintsOnMouseActivity(true);
}

void RoutingSelector::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    auto mainArea = getMainButtonArea().toFloat();
    auto dropdownArea = getDropdownArea().toFloat();

    // Background: always use BUTTON_NORMAL, brighter on hover
    auto bgColour = DarkTheme::getColour(DarkTheme::BUTTON_NORMAL);
    if (isHovering_) {
        bgColour = bgColour.brighter(0.1f);
    }

    // Draw main button area
    g.setColour(bgColour);
    g.fillRect(mainArea);

    // Draw dropdown area (slightly different shade)
    g.setColour(bgColour.darker(0.1f));
    g.fillRect(dropdownArea);

    // Draw separator line between main and dropdown
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawLine(dropdownArea.getX(), dropdownArea.getY() + 2, dropdownArea.getX(),
               dropdownArea.getBottom() - 2, 1.0f);

    // Draw selected name as text in main area
    auto textBounds = mainArea.reduced(2.0f, 1.0f);
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    g.setFont(FontManager::getInstance().getUIFont(9.0f));
    g.drawText(getSelectedName(), textBounds, juce::Justification::centredLeft, true);

    // Draw dropdown arrow
    auto arrowBounds = dropdownArea.reduced(2.0f);
    float arrowSize = std::min(arrowBounds.getWidth(), arrowBounds.getHeight()) * 0.4f;
    float arrowX = arrowBounds.getCentreX();
    float arrowY = arrowBounds.getCentreY();

    juce::Path arrow;
    arrow.addTriangle(arrowX - arrowSize, arrowY - arrowSize * 0.5f, arrowX + arrowSize,
                      arrowY - arrowSize * 0.5f, arrowX, arrowY + arrowSize * 0.5f);

    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    g.fillPath(arrow);

    // Draw border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(bounds, 1.0f);
}

void RoutingSelector::resized() {
    // Layout is calculated in getMainButtonArea() and getDropdownArea()
}

void RoutingSelector::mouseDown(const juce::MouseEvent& e) {
    juce::ignoreUnused(e);
    showPopupMenu();
}

void RoutingSelector::mouseEnter(const juce::MouseEvent&) {
    isHovering_ = true;
    repaint();
}

void RoutingSelector::mouseExit(const juce::MouseEvent&) {
    isHovering_ = false;
    repaint();
}

void RoutingSelector::setEnabled(bool shouldBeEnabled) {
    if (enabled_ != shouldBeEnabled) {
        enabled_ = shouldBeEnabled;
        repaint();
    }
}

void RoutingSelector::setSelectedId(int id) {
    if (selectedId_ != id) {
        selectedId_ = id;
        repaint();
    }
}

juce::String RoutingSelector::getSelectedName() const {
    for (const auto& opt : options_) {
        if (opt.id == selectedId_) {
            return opt.name;
        }
    }
    return "None";
}

void RoutingSelector::setOptions(const std::vector<RoutingOption>& options) {
    options_ = options;
    // Auto-select first non-separator option if nothing selected
    if (selectedId_ < 0 && !options_.empty()) {
        for (const auto& opt : options_) {
            if (!opt.isSeparator) {
                selectedId_ = opt.id;
                break;
            }
        }
    }
}

void RoutingSelector::clearOptions() {
    options_.clear();
    selectedId_ = -1;
}

int RoutingSelector::getFirstChannelOptionId() const {
    for (const auto& opt : options_) {
        if (!opt.isSeparator && opt.id >= 10) {
            return opt.id;
        }
    }
    return -1;
}

juce::Rectangle<int> RoutingSelector::getMainButtonArea() const {
    auto bounds = getLocalBounds();
    return bounds.withTrimmedRight(DROPDOWN_ARROW_WIDTH);
}

juce::Rectangle<int> RoutingSelector::getDropdownArea() const {
    auto bounds = getLocalBounds();
    return bounds.removeFromRight(DROPDOWN_ARROW_WIDTH);
}

void RoutingSelector::showPopupMenu() {
    juce::PopupMenu menu;

    // Add routing options
    if (options_.empty()) {
        menu.addItem(-1, "(No options available)", false);
    } else {
        for (const auto& opt : options_) {
            if (opt.isSeparator) {
                menu.addSeparator();
            } else {
                menu.addItem(opt.id, opt.name, true, opt.id == selectedId_);
            }
        }
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMinimumWidth(100),
                       [this](int result) {
                           if (result == 0) {
                               return;  // Dismissed
                           }
                           if (result > 0) {
                               // Selection changed
                               setSelectedId(result);
                               if (onSelectionChanged) {
                                   onSelectionChanged(result);
                               }
                           }
                       });
}

}  // namespace magda
