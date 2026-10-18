#include "compiled/DrumVoiceLayout.hpp"

#include <algorithm>

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

int rowsOf(const DrumVoiceSection& section) {
    const int columns = std::max(1, section.columns);
    return (static_cast<int>(section.slots.size()) + columns - 1) / columns;
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

std::vector<DrumVoiceSection> drumVoiceSections(const juce::String& pluginId) {
    auto sections = voiceLayers(pluginId);
    if (sections.empty())
        return sections;
    // Gain is the wrapper's slot after the voice's own, so it follows the highest pinned one.
    int last = 0;
    for (const auto& section : sections)
        for (const int slot : section.slots)
            last = std::max(last, slot);
    sections.push_back({.title = "Output", .slots = {last + 1}, .columns = 1, .underPlot = true});
    return sections;
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
        if (!section.underPlot)
            width += section.columns * kCellWidth + (section.columns - 1) * kCellGap + kSpace;
    return width;
}

DrumVoiceGeometry DrumVoiceGeometry::of(juce::Rectangle<int> bounds, ParamControlStyle style,
                                        const std::vector<DrumVoiceSection>& sections) {
    DrumVoiceGeometry g;
    auto area = bounds.reduced(kSpace);
    auto columns = area.removeFromRight(
        std::min(columnsWidth(sections), std::max(0, area.getWidth() - kMinPlotWidth)));
    area.removeFromRight(kSpace);

    int rows = 1;
    for (const auto& section : sections)
        if (!section.underPlot)
            rows = std::max(rows, rowsOf(section));
    const int available = columns.getHeight() - kTitleHeight - (rows - 1) * kCellGap;
    const int cellH = std::min(cellHeight(style), std::max(24, available / rows));

    // Sections under the plot sit in a row along its foot; the plot takes what is left above.
    auto foot = area.removeFromBottom(cellH);
    area.removeFromBottom(kSpace);
    g.plot = area;

    for (const auto& section : sections) {
        if (section.underPlot) {
            g.titles.emplace_back();
            for (size_t i = 0; i < section.slots.size(); ++i) {
                g.cells.push_back(foot.removeFromLeft(kCellWidth));
                foot.removeFromLeft(kCellGap);
            }
            continue;
        }
        const int width = section.columns * kCellWidth + (section.columns - 1) * kCellGap;
        auto column = columns.removeFromLeft(width + kSpace).withTrimmedLeft(kSpace);
        g.titles.push_back(column.removeFromTop(kTitleHeight));
        for (size_t i = 0; i < section.slots.size(); ++i) {
            const int col = static_cast<int>(i) % section.columns;
            const int row = static_cast<int>(i) / section.columns;
            g.cells.emplace_back(column.getX() + col * (kCellWidth + kCellGap),
                                 column.getY() + row * (cellH + kCellGap), kCellWidth, cellH);
        }
    }
    return g;
}

DrumVoiceLayout::DrumVoiceLayout(const juce::String& pluginId)
    : sections_(drumVoiceSections(pluginId)) {
    for (const auto& section : sections_)
        slots_.insert(slots_.end(), section.slots.begin(), section.slots.end());
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

ParamCell DrumVoiceLayout::cellFor(const magda::DeviceInfo& device, int cellIndex, int) const {
    ParamCell cell;
    if (cellIndex < 0 || cellIndex >= cellCount())
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
        return cell;
    }
    return cell;
}

}  // namespace magda::daw::ui
