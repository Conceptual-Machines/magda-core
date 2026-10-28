// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

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

void Decor::clear() {
    wells.clear();
    dividers.clear();
}

void Decor::paint(juce::Graphics& g, int toolbarHeight) const {
    for (const auto& well : wells)
        paintWell(g, well);
    for (int x : dividers)
        paintDivider(g, x, toolbarHeight);
}

Row::Row(Decor& decor, juce::Rectangle<int> bounds, int startX, From from)
    : decor_(&decor), bounds_(bounds), x_(startX), from_(from) {}

void Row::centred(Decor& decor, juce::Rectangle<int> bounds,
                  const std::function<void(Row&)>& build) {
    Decor scratch;
    Row probe(scratch, bounds, 0, From::Left);
    build(probe);
    Row row(decor, bounds, bounds.getCentreX() - probe.position() / 2, From::Left);
    build(row);
}

juce::Rectangle<int> Row::take(int width, int height) {
    if (wellStart_ >= 0 && wellHasItem_)
        space(kGap);
    wellHasItem_ = true;
    if (from_ == From::Right)
        x_ -= width;
    const juce::Rectangle<int> area{x_, bounds_.getCentreY() - height / 2, width, height};
    if (from_ == From::Left)
        x_ += width;
    return area;
}

Row& Row::button(juce::Component& component) {
    return item(component, kButton, kButton);
}

Row& Row::item(juce::Component& component, int width, int height) {
    component.setBounds(take(width, height));
    return *this;
}

Row& Row::space(int px) {
    x_ += from_ == From::Left ? px : -px;
    return *this;
}

Row& Row::divider() {
    space(8);
    decor_->dividers.push_back(x_);
    return space(8);
}

Row& Row::openWell() {
    wellStart_ = x_;
    wellHasItem_ = false;
    return space(kWellPad);
}

Row& Row::closeWell() {
    space(kWellPad);
    const int height = kButton + kWellPad * 2;
    decor_->wells.emplace_back(juce::jmin(wellStart_, x_), bounds_.getCentreY() - height / 2,
                               std::abs(x_ - wellStart_), height);
    wellStart_ = -1;
    return *this;
}

}  // namespace edit_toolbar

namespace {

std::vector<std::pair<EditTool, const char*>> allTools(const std::array<const char*, 5>& tooltips) {
    return {{EditTool::Pointer, tooltips[0]},
            {EditTool::Pencil, tooltips[1]},
            {EditTool::Slice, tooltips[2]},
            {EditTool::Glue, tooltips[3]},
            {EditTool::Erase, tooltips[4]}};
}

}  // namespace

EditToolButtons::EditToolButtons(EditToolState& state, const std::array<const char*, 5>& tooltips)
    : EditToolButtons(state, allTools(tooltips)) {}

EditToolButtons::EditToolButtons(EditToolState& state,
                                 const std::vector<std::pair<EditTool, const char*>>& tools)
    : state_(state) {
    static constexpr std::array<const char*, 9> kNames{
        "Pointer", "Pencil", "Slice", "Glue", "Erase", "Line", "Flat", "Marker", "Label"};
    const std::array<std::pair<const char*, int>, 9> icons{{
        {BinaryData::mepointer_svg, BinaryData::mepointer_svgSize},
        {BinaryData::mepencil_svg, BinaryData::mepencil_svgSize},
        {BinaryData::meslice_svg, BinaryData::meslice_svgSize},
        {BinaryData::meglue_svg, BinaryData::meglue_svgSize},
        {BinaryData::meerase_svg, BinaryData::meerase_svgSize},
        {BinaryData::mepointer_svg, BinaryData::mepointer_svgSize},  // Line: the arrow
        {BinaryData::meflat_svg, BinaryData::meflat_svgSize},
        {BinaryData::memarker_svg, BinaryData::memarker_svgSize},
        {BinaryData::melabel_svg, BinaryData::melabel_svgSize},
    }};
    for (const auto& [tool, tooltip] : tools) {
        const auto i = static_cast<size_t>(tool);
        auto button = std::make_unique<SvgButton>(kNames[i], icons[i].first,
                                                  static_cast<size_t>(icons[i].second));
        button->setTooltip(tooltip);
        edit_toolbar::styleButton(*button);
        button->onClick = [this, tool]() { state_.setTool(tool); };
        addAndMakeVisible(button.get());
        buttons_.emplace_back(tool, std::move(button));
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
    for (auto& [tool, button] : buttons_) {
        button->setBounds(x, edit_toolbar::kWellPad, edit_toolbar::kButton, edit_toolbar::kButton);
        x += edit_toolbar::kButton + edit_toolbar::kGap;
    }
}

void EditToolButtons::setToolEnabled(EditTool tool, bool enabled) {
    for (auto& [each, button] : buttons_)
        if (each == tool)
            button->setEnabled(enabled);
}

void EditToolButtons::changeListenerCallback(juce::ChangeBroadcaster*) {
    syncButtons();
}

void EditToolButtons::syncButtons() {
    for (auto& [tool, button] : buttons_)
        button->setActive(state_.getTool() == tool);
}

}  // namespace magda
