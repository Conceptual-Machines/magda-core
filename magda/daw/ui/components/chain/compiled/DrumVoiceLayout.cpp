// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "compiled/DrumVoiceLayout.hpp"

#include <algorithm>

#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

std::vector<DrumVoiceSection> voiceLayers(const juce::String& pluginId) {
    if (pluginId.equalsIgnoreCase("magda_kick"))
        return {{.title = "Transient", .slots = {0, 1, 2, 3, 13}, .decaySlot = 3, .curveSlot = 13},
                {.title = "Body",
                 .slots = {4, 5, 6, 7, 8, 9, 12},
                 .attackSlot = 7,
                 .decaySlot = 8,
                 .curveSlot = 12,
                 .columns = 3},
                {.title = "Click", .slots = {10, 11}, .columns = 1}};
    if (pluginId.equalsIgnoreCase("magda_snare"))
        return {
            {.title = "Transient", .slots = {0, 1, 2, 3, 4, 17}, .decaySlot = 3, .curveSlot = 17},
            {.title = "Body",
             .slots = {5, 6, 7, 8, 9, 16},
             .attackSlot = 8,
             .decaySlot = 9,
             .curveSlot = 16},
            {.title = "Rattle",
             .slots = {10, 11, 12, 13, 14, 15, 18},
             .decaySlot = 14,
             .curveSlot = 18,
             .columns = 3}};
    if (pluginId.equalsIgnoreCase("magda_clap"))
        return {{.title = "Flam", .slots = {0, 1, 2, 3, 7}, .decaySlot = 2, .curveSlot = 7},
                {.title = "Shape", .slots = {4, 5, 6}, .columns = 1}};
    if (pluginId.equalsIgnoreCase("magda_hat"))
        return {{.title = "Ring", .slots = {0, 1, 2, 3, 7}, .decaySlot = 3, .curveSlot = 7},
                {.title = "Noise", .slots = {4, 5, 9, 10, 6, 8}, .decaySlot = 6, .curveSlot = 8}};
    if (pluginId.equalsIgnoreCase("magda_tom"))
        return {{.title = "Body",
                 .slots = {0, 1, 2, 3, 7},
                 .attackSlot = 2,
                 .decaySlot = 3,
                 .curveSlot = 7},
                {.title = "Noise", .slots = {4, 5, 6, 8}, .decaySlot = 6, .curveSlot = 8}};
    return {};
}

// The curve rides in its decay cell, so it takes no grid position.
int gridCountOf(const DrumVoiceSection& section) {
    const auto count = static_cast<int>(section.slots.size());
    return section.curveSlot >= 0 ? count - 1 : count;
}

int rowsOf(const DrumVoiceSection& section) {
    const int columns = std::max(1, section.columns);
    return (gridCountOf(section) + columns - 1) / columns;
}

/// "body_snap_time" -> "Snap time": the layer's title already names the rest.
juce::String roleLabel(const juce::String& hostName) {
    auto role = hostName.fromFirstOccurrenceOf("_", false, false);
    if (role.isEmpty())
        role = hostName;
    role = role.replaceCharacter('_', ' ');
    role = role.replace("hp ", "HP ").replace("Hp ", "HP ");
    return role.substring(0, 1).toUpperCase() + role.substring(1);
}

}  // namespace

// The wrapper's trailing Gain stays off the faceplate; the side strip's fader sets the level.
std::vector<DrumVoiceSection> drumVoiceSections(const juce::String& pluginId) {
    return voiceLayers(pluginId);
}

int DrumVoiceGeometry::cellHeight(ParamControlStyle style) {
    switch (style) {
        case ParamControlStyle::Knobs:
            return 72;
        case ParamControlStyle::Sliders:
            return 44;
        case ParamControlStyle::Text:
            break;
    }
    return 48;
}

int DrumVoiceGeometry::columnsWidth(const std::vector<DrumVoiceSection>& sections) {
    int width = 0;
    for (const auto& section : sections)
        width = std::max(width, section.columns * kCellWidth + (section.columns - 1) * kCellGap);
    return width + kSpace;
}

DrumVoiceGeometry DrumVoiceGeometry::of(juce::Rectangle<int> bounds, ParamControlStyle style,
                                        const std::vector<DrumVoiceSection>& sections,
                                        bool cellsShown) {
    DrumVoiceGeometry g;
    if (!cellsShown) {
        g.plot = bounds.reduced(kSpace);
        return g;
    }
    // The columns' own leading space stands in for the body's left padding.
    auto area = bounds.reduced(kSpace).withTrimmedLeft(-kSpace);
    auto columns = area.removeFromLeft(std::min(columnsWidth(sections), area.getWidth()));
    area.removeFromLeft(kSpace);
    if (area.getWidth() >= kMinPlotWidth)
        g.plot = area;

    columns.removeFromLeft(kSpace);
    auto tabs = columns.removeFromTop(kTitleHeight);
    const auto font = FontManager::getInstance().getMonoFont(10.0f).withExtraKerningFactor(0.12f);
    for (const auto& section : sections) {
        const auto title = juce::String(section.title).toUpperCase();
        g.titles.push_back(
            tabs.removeFromLeft(juce::GlyphArrangement::getStringWidthInt(font, title)));
        tabs.removeFromLeft(14);
    }
    columns.removeFromTop(kCellGap);

    for (const auto& section : sections) {
        const int rows = rowsOf(section);
        const int available = columns.getHeight() - (rows - 1) * kCellGap;
        const int cellH = std::min(cellHeight(style), std::max(24, available / rows));
        int position = 0;
        juce::Rectangle<int> decayCell;
        for (const int slot : section.slots) {
            if (slot == section.curveSlot) {
                g.cells.emplace_back(decayCell.getRight() - 6 - kChipWidth, decayCell.getY() + 5,
                                     kChipWidth, kChipHeight);
                continue;
            }
            const int col = position % section.columns;
            const int row = position / section.columns;
            ++position;
            g.cells.emplace_back(columns.getX() + col * (kCellWidth + kCellGap),
                                 columns.getY() + row * (cellH + kCellGap), kCellWidth, cellH);
            if (slot == section.decaySlot)
                decayCell = g.cells.back();
        }
    }
    return g;
}

DrumVoiceLayout::DrumVoiceLayout(const juce::String& pluginId)
    : sections_(drumVoiceSections(pluginId)) {
    for (size_t i = 0; i < sections_.size(); ++i) {
        const auto& section = sections_[i];
        slots_.insert(slots_.end(), section.slots.begin(), section.slots.end());
        sectionOfCell_.insert(sectionOfCell_.end(), section.slots.size(), static_cast<int>(i));
        if (section.curveSlot >= 0)
            curveSlots_.push_back(section.curveSlot);
    }
}

int DrumVoiceLayout::cellCount() const {
    return static_cast<int>(slots_.size());
}

juce::Rectangle<int> DrumVoiceLayout::cellBounds(int cell, juce::Rectangle<int> area, bool) const {
    const auto geometry = DrumVoiceGeometry::of(area, style_, sections_);
    return cell >= 0 && cell < static_cast<int>(geometry.cells.size())
               ? geometry.cells[static_cast<size_t>(cell)]
               : juce::Rectangle<int>();
}

ParamCell DrumVoiceLayout::cellFor(const magda::DeviceInfo& device, int cellIndex, int page) const {
    ParamCell cell;
    // Only the selected layer's cells show.
    if (cellIndex < 0 || cellIndex >= cellCount() ||
        sectionOfCell_[static_cast<size_t>(cellIndex)] != page)
        return cell;
    const int slot = slots_[static_cast<size_t>(cellIndex)];
    for (int k = 0; k < static_cast<int>(device.parameters.size()); ++k) {
        const auto& param = device.parameters[static_cast<size_t>(k)];
        if (param.paramIndex != slot)
            continue;
        cell.mode = ParamCell::Mode::Filled;
        cell.paramArrayIndex = k;
        cell.targetParamIndex = slot;
        cell.label = roleLabel(param.name);
        cell.chip = std::ranges::find(curveSlots_, slot) != curveSlots_.end();
        return cell;
    }
    return cell;
}

}  // namespace magda::daw::ui
