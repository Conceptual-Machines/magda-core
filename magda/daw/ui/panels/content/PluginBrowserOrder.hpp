// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file PluginBrowserOrder.hpp
 * @brief Where the device browser files an entry, and the order it lists them in (#3417).
 */

#pragma once

#include "PluginBrowserContent.hpp"

namespace magda::daw::ui {

/// The category of the browser's rack entries, which are neither instruments nor effects.
inline constexpr const char* kRackBrowserCategory = "Rack";

inline juce::String effectiveCategoryForPlugin(const PluginBrowserInfo& plugin) {
    if (plugin.categoryOverride == "Instrument")
        return "Instrument";
    if (plugin.categoryOverride == "MIDI FX" || plugin.categoryOverride == "Audio FX")
        return "Effect";
    if (plugin.categoryOverride == "Analyzer")
        return "Analysis";
    return plugin.category;
}

inline juce::String effectiveSubcategoryForPlugin(const PluginBrowserInfo& plugin) {
    if (plugin.categoryOverride == "MIDI FX")
        return "MIDI";
    if (plugin.categoryOverride == "Audio FX")
        return "Audio FX";
    if (plugin.categoryOverride == "Analyzer")
        return "Analyzer";
    return plugin.subcategory;
}

inline bool effectiveIsInstrument(const PluginBrowserInfo& plugin) {
    if (plugin.categoryOverride == "Instrument")
        return true;
    if (plugin.categoryOverride.isNotEmpty())
        return false;
    return plugin.category == "Instrument";
}

/** @brief The By Category folder @p plugin is listed in. */
inline juce::String categoryFolderFor(const PluginBrowserInfo& plugin) {
    if (plugin.category == kRackBrowserCategory)
        return "Tools";
    if (effectiveIsInstrument(plugin))
        return "Instrument";
    const auto subcategory = effectiveSubcategoryForPlugin(plugin);
    if (subcategory == "MIDI")
        return "MIDI FX";
    if (subcategory == "Analysis" || subcategory == "Analyzer")
        return "Analyzer";
    return "Audio FX";
}

/** @brief The subfolder of categoryFolderFor() @p plugin sits in; empty for the folder itself. */
inline juce::String categorySubfolderFor(const PluginBrowserInfo& plugin) {
    const auto folder = categoryFolderFor(plugin);
    if (folder != "Instrument" && folder != "Audio FX")
        return {};
    const auto subcategory = effectiveSubcategoryForPlugin(plugin);
    return subcategory == folder ? juce::String() : subcategory;
}

/** @brief Orders two names ignoring case, falling back to case so the order is total. */
inline int compareBrowserNames(const juce::String& a, const juce::String& b) {
    if (const int byName = a.compareIgnoreCase(b); byName != 0)
        return byName;
    return a.compare(b);
}

/** @brief Orders folder names: By Category's top folders in a fixed order, the rest by name. */
inline int compareBrowserFolders(const juce::String& a, const juce::String& b,
                                 bool categoryTopLevel) {
    if (categoryTopLevel) {
        static const juce::StringArray order{"Instrument", "MIDI FX", "Audio FX", "Analyzer",
                                             "Tools"};
        const auto rank = [](const juce::String& name) {
            const int index = order.indexOf(name);
            return index >= 0 ? index : order.size();
        };
        if (const int byRank = rank(a) - rank(b); byRank != 0)
            return byRank;
    }
    return compareBrowserNames(a, b);
}

/** @brief Whether @p a is listed before @p b inside a folder: by name, then by format. */
inline bool browserEntryBefore(const PluginBrowserInfo& a, const PluginBrowserInfo& b) {
    if (const int byName = compareBrowserNames(a.name, b.name); byName != 0)
        return byName < 0;
    return compareBrowserNames(a.format, b.format) < 0;
}

}  // namespace magda::daw::ui
