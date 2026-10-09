#include "EditToolbar.hpp"

#include "../../themes/ActiveTheme.hpp"
#include "BinaryData.h"
#include "SvgButton.hpp"

namespace magda {

namespace edit_toolbar {

void styleButton(SvgButton& button) {
    button.setOriginalColor(juce::Colour(0xFFB3B3B3));
    button.setNormalColor(ActiveTheme::TEXT_SECONDARY);
    button.setHoverColor(ActiveTheme::TEXT_PRIMARY);
    button.setActiveColor(ActiveTheme::TEXT_PRIMARY);
    button.setActiveBackgroundColor(ActiveTheme::MIDI_TOOL_ACTIVE);
    button.setActiveBorderColor(ActiveTheme::MIDI_TOOL_ACTIVE_BORDER);
    button.setHoverBackgroundColor(ActiveTheme::MIDI_TOOL_ACTIVE);
    button.setBorderThickness(1.0f);
    button.setCornerRadius(6.0f);
    button.setIconPadding(7.0f);
}

void paintWell(juce::Graphics& g, juce::Rectangle<int> well) {
    g.setColour(ActiveTheme::getColour(ActiveTheme::MIDI_LANE));
    g.fillRoundedRectangle(well.toFloat(), 7.0f);
}

void paintDivider(juce::Graphics& g, int x, int toolbarHeight) {
    g.setColour(ActiveTheme::getColour(ActiveTheme::BORDER));
    g.fillRect(x, 10, 1, toolbarHeight - 20);
}

}  // namespace edit_toolbar

EditToolButtons::EditToolButtons(EditToolState& state, const std::array<const char*, 5>& tooltips)
    : state_(state) {
    static constexpr std::array<const char*, 5> kNames{"Pointer", "Pencil", "Slice", "Glue",
                                                       "Erase"};
    const std::array<std::pair<const char*, int>, 5> icons{{
        {BinaryData::mepointer_svg, BinaryData::mepointer_svgSize},
        {BinaryData::mepencil_svg, BinaryData::mepencil_svgSize},
        {BinaryData::meslice_svg, BinaryData::meslice_svgSize},
        {BinaryData::meglue_svg, BinaryData::meglue_svgSize},
        {BinaryData::meerase_svg, BinaryData::meerase_svgSize},
    }};
    for (size_t i = 0; i < buttons_.size(); ++i) {
        auto button = std::make_unique<SvgButton>(kNames[i], icons[i].first,
                                                  static_cast<size_t>(icons[i].second));
        button->setTooltip(tooltips[i]);
        edit_toolbar::styleButton(*button);
        button->onClick = [this, i]() { state_.setTool(static_cast<EditTool>(i)); };
        addAndMakeVisible(button.get());
        buttons_[i] = std::move(button);
    }
    state_.addChangeListener(this);
    syncButtons();
}

EditToolButtons::~EditToolButtons() {
    state_.removeChangeListener(this);
}

void EditToolButtons::paint(juce::Graphics& g) {
    edit_toolbar::paintWell(g, getLocalBounds());
}

void EditToolButtons::resized() {
    int x = edit_toolbar::kWellPad;
    for (auto& button : buttons_) {
        button->setBounds(x, edit_toolbar::kWellPad, edit_toolbar::kButton, edit_toolbar::kButton);
        x += edit_toolbar::kButton + edit_toolbar::kGap;
    }
}

void EditToolButtons::changeListenerCallback(juce::ChangeBroadcaster*) {
    syncButtons();
}

void EditToolButtons::syncButtons() {
    const auto tool = state_.getTool();
    for (size_t i = 0; i < buttons_.size(); ++i)
        buttons_[i]->setActive(static_cast<size_t>(tool) == i);
}

}  // namespace magda
