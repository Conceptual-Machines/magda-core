#include "MonitorSelector.hpp"

#include "../../../core/StringTable.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "BinaryData.h"

namespace magda {

// The speaker glyph ships with a flat #B3B3B3 fill; we recolour a copy per
// paint to tint by monitor state (green when active, grey when off).
static constexpr juce::uint32 kSpeakerSourceColour = 0xFFB3B3B3;

MonitorSelector::MonitorSelector() {
    setRepaintsOnMouseActivity(true);
    speakerIcon_ =
        juce::Drawable::createFromImageData(BinaryData::speaker_svg, BinaryData::speaker_svgSize);
    speakerOffIcon_ = juce::Drawable::createFromImageData(BinaryData::speaker_simple_svg,
                                                          BinaryData::speaker_simple_svgSize);
    setTooltip(tr("tracks.input_monitoring") + " (" + tr("tracks.input_monitoring.off") + "/" +
               tr("tracks.input_monitoring.in") + "/" + tr("tracks.input_monitoring.auto") + ")");
}

InputMonitorMode MonitorSelector::nextMode(InputMonitorMode mode) {
    switch (mode) {
        case InputMonitorMode::Off:
            return InputMonitorMode::In;
        case InputMonitorMode::In:
            return InputMonitorMode::Auto;
        case InputMonitorMode::Auto:
        default:
            return InputMonitorMode::Off;
    }
}

void MonitorSelector::setMode(InputMonitorMode mode) {
    if (mode_ != mode) {
        mode_ = mode;
        repaint();
    }
}

juce::Rectangle<int> MonitorSelector::getIconArea() const {
    // Everything left of the dropdown arrow — clicking it cycles the mode.
    return getLocalBounds().withTrimmedRight(DROPDOWN_ARROW_WIDTH);
}

juce::Rectangle<int> MonitorSelector::getDropdownArea() const {
    auto bounds = getLocalBounds();
    return bounds.removeFromRight(DROPDOWN_ARROW_WIDTH);
}

void MonitorSelector::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    auto iconArea = getIconArea().toFloat();
    auto dropdownArea = getDropdownArea().toFloat();

    auto bgColour = DarkTheme::getColour(DarkTheme::BUTTON_NORMAL);
    if (isHovering_)
        bgColour = bgColour.brighter(0.1f);

    // Body + slightly darker dropdown gutter, matching RoutingSelector.
    g.setColour(bgColour);
    g.fillRect(bounds);
    g.setColour(bgColour.darker(0.1f));
    g.fillRect(dropdownArea);

    // Separator before the dropdown arrow.
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawLine(dropdownArea.getX(), dropdownArea.getY() + 2, dropdownArea.getX(),
               dropdownArea.getBottom() - 2, 1.0f);

    // Off uses the plain (no-waves) speaker; In / Auto use the waved speaker,
    // tinted green / blue. Colour conveys the active state.
    if (auto* icon = isActiveMode() ? speakerIcon_.get() : speakerOffIcon_.get()) {
        const auto tint =
            mode_ == InputMonitorMode::In     ? DarkTheme::getColour(DarkTheme::ACCENT_GREEN)
            : mode_ == InputMonitorMode::Auto ? DarkTheme::getColour(DarkTheme::ACCENT_BLUE)
                                              : DarkTheme::getColour(DarkTheme::TEXT_PRIMARY);
        auto iconCopy = icon->createCopy();
        iconCopy->replaceColour(juce::Colour(kSpeakerSourceColour), tint);
        iconCopy->drawWithin(g, iconArea.reduced(3.0f), juce::RectanglePlacement::centred, 1.0f);
    }

    // Dropdown arrow.
    auto arrowBounds = dropdownArea.reduced(2.0f);
    float arrowSize = std::min(arrowBounds.getWidth(), arrowBounds.getHeight()) * 0.4f;
    float arrowX = arrowBounds.getCentreX();
    float arrowY = arrowBounds.getCentreY();
    juce::Path arrow;
    arrow.addTriangle(arrowX - arrowSize, arrowY - arrowSize * 0.5f, arrowX + arrowSize,
                      arrowY - arrowSize * 0.5f, arrowX, arrowY + arrowSize * 0.5f);
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    g.fillPath(arrow);

    // Border.
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(bounds, 1.0f);
}

void MonitorSelector::mouseDown(const juce::MouseEvent& e) {
    // Clicking the speaker icon cycles; anywhere else opens the menu.
    if (getIconArea().contains(e.getPosition())) {
        if (onModeChanged)
            onModeChanged(nextMode(mode_));
    } else {
        showPopupMenu();
    }
}

void MonitorSelector::mouseEnter(const juce::MouseEvent&) {
    isHovering_ = true;
    repaint();
}

void MonitorSelector::mouseExit(const juce::MouseEvent&) {
    isHovering_ = false;
    repaint();
}

void MonitorSelector::showPopupMenu() {
    juce::PopupMenu menu;
    menu.addItem(1, tr("tracks.input_monitoring.off"), true, mode_ == InputMonitorMode::Off);
    menu.addItem(2, tr("tracks.input_monitoring.in"), true, mode_ == InputMonitorMode::In);
    menu.addItem(3, tr("tracks.input_monitoring.auto"), true, mode_ == InputMonitorMode::Auto);

    juce::Component::SafePointer<MonitorSelector> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMinimumWidth(90),
                       [safeThis](int result) {
                           if (safeThis == nullptr || result == 0)
                               return;
                           const auto mode = result == 1   ? InputMonitorMode::Off
                                             : result == 2 ? InputMonitorMode::In
                                                           : InputMonitorMode::Auto;
                           if (safeThis->onModeChanged)
                               safeThis->onModeChanged(mode);
                       });
}

}  // namespace magda
