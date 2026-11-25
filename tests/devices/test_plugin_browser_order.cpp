// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "magda/daw/ui/panels/content/PluginBrowserOrder.hpp"

// Where the device browser files an entry and the order it lists them in (#3417).

namespace {

using namespace magda::daw::ui;

PluginBrowserInfo entry(const juce::String& name, const juce::String& category,
                        const juce::String& subcategory, const juce::String& format = "VST3") {
    PluginBrowserInfo info;
    info.name = name;
    info.category = category;
    info.subcategory = subcategory;
    info.format = format;
    return info;
}

}  // namespace

TEST_CASE("By Category files each entry under one of its fixed folders", "[browser][order]") {
    CHECK(categoryFolderFor(entry("Synth", "Instrument", "Synth")) == "Instrument");
    CHECK(categorySubfolderFor(entry("Synth", "Instrument", "Synth")) == "Synth");
    CHECK(categoryFolderFor(entry("Arp", "Effect", "MIDI")) == "MIDI FX");
    CHECK(categorySubfolderFor(entry("Arp", "Effect", "MIDI")).isEmpty());
    CHECK(categoryFolderFor(entry("Scope", "Effect", "Analysis")) == "Analyzer");
    CHECK(categoryFolderFor(entry("EQ", "Effect", "EQ")) == "Audio FX");
    CHECK(categorySubfolderFor(entry("EQ", "Effect", "EQ")) == "EQ");
    CHECK(categoryFolderFor(entry("Rack", kRackBrowserCategory, kRackBrowserCategory)) == "Tools");

    auto routed = entry("Keys", "Instrument", "Synth");
    routed.categoryOverride = "MIDI FX";
    CHECK(categoryFolderFor(routed) == "MIDI FX");
    routed.categoryOverride = "Audio FX";
    CHECK(categoryFolderFor(routed) == "Audio FX");
    CHECK(categorySubfolderFor(routed).isEmpty());
}

TEST_CASE("By Category's folders keep their fixed order; any other folder sorts by name",
          "[browser][order]") {
    std::vector<juce::String> folders{"Tools", "Audio FX", "Analyzer", "MIDI FX", "Instrument"};
    std::ranges::sort(folders, [](const auto& a, const auto& b) {
        return compareBrowserFolders(a, b, true) < 0;
    });
    CHECK(folders ==
          std::vector<juce::String>{"Instrument", "MIDI FX", "Audio FX", "Analyzer", "Tools"});

    std::vector<juce::String> vendors{"u-he", "MAGDA", "Arturia", "apple"};
    std::ranges::sort(vendors, [](const auto& a, const auto& b) {
        return compareBrowserFolders(a, b, false) < 0;
    });
    CHECK(vendors == std::vector<juce::String>{"apple", "Arturia", "MAGDA", "u-he"});
}

TEST_CASE("Entries sort by name ignoring case, then by format", "[browser][order]") {
    std::vector<PluginBrowserInfo> entries{
        entry("zebra", "Instrument", "Synth"), entry("Diva", "Instrument", "Synth", "VST3"),
        entry("arp", "Effect", "MIDI"), entry("Diva", "Instrument", "Synth", "AU")};
    std::ranges::stable_sort(entries, browserEntryBefore);

    std::vector<juce::String> order;
    for (const auto& e : entries)
        order.push_back(e.name + " " + e.format);
    CHECK(order == std::vector<juce::String>{"arp VST3", "Diva AU", "Diva VST3", "zebra VST3"});
}
