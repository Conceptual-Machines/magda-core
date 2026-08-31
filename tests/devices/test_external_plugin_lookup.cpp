#include <juce_audio_processors/juce_audio_processors.h>

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/audio/plugin_manager/ExternalPluginLookup.hpp"

/**
 * @file test_external_plugin_lookup.cpp
 * @brief Which installed plugin a saved device meant (#2243).
 *
 * The four passes, each shown resolving the case it was added for and shown not
 * resolving the case the pass before it owns. The order is the whole content of
 * this function: every pass matches things the next one also matches, so a test
 * that only checked "it finds something" would pass with them in any order and
 * would let the wrong plugin load.
 */

namespace {

juce::PluginDescription installed(const juce::String& name, const juce::String& vendor,
                                  const juce::String& file, bool isInstrument,
                                  const juce::String& format = "VST3") {
    juce::PluginDescription description;
    description.name = name;
    description.manufacturerName = vendor;
    description.fileOrIdentifier = file;
    description.isInstrument = isInstrument;
    description.pluginFormatName = format;
    description.uniqueId = name.hashCode() + (isInstrument ? 1 : 0);
    return description;
}

magda::DeviceInfo saved(const juce::String& name, const juce::String& vendor,
                        const juce::String& file, bool isInstrument,
                        magda::PluginFormat format = magda::PluginFormat::VST3) {
    magda::DeviceInfo device;
    device.name = name;
    device.manufacturer = vendor;
    device.fileOrIdentifier = file;
    device.isInstrument = isInstrument;
    device.format = format;
    return device;
}

/// juce::KnownPluginList cannot be copied or moved, so it is filled in place
/// rather than returned.
void fill(juce::KnownPluginList& list, const std::vector<juce::PluginDescription>& descriptions) {
    for (const auto& description : descriptions)
        list.addType(description);
}

}  // namespace

TEST_CASE("JUCE's own identifier is the first answer", "[plugins][lookup]") {
    // A bundle shipping several effects out of one file, which is what a shell
    // format is. Every one of them has the same path and the same role, so the
    // file pass returns whichever the scan listed first; only the identifier
    // tells them apart, and it is what the project saved.
    // The decoy is added last, which puts it first: KnownPluginList::addType
    // inserts at the front. So the file pass below, which cannot tell these two
    // apart, returns the decoy.
    juce::KnownPluginList list;
    const auto wanted = installed("Shell Reverb", "Vendor", "/Library/Shell.vst3", false);
    const auto decoy = installed("Shell Delay", "Vendor", "/Library/Shell.vst3", false);
    fill(list, {wanted, decoy});

    auto device = saved("Shell Reverb", "Vendor", "/Library/Shell.vst3", false);
    device.uniqueId = wanted.createIdentifierString();

    const auto match = magda::matchInstalledPlugin(device, list);

    REQUIRE(match.found);
    CHECK(match.description.name == "Shell Reverb");
}

TEST_CASE("The file and the instrument flag together are the second answer", "[plugins][lookup]") {
    // One file, two plugins out of it: the effect and the instrument. This is
    // the case the flag is in the first pass for, and loading the other one is
    // silent rather than an error.
    juce::KnownPluginList list;
    fill(list, {installed("Kontakt", "NI", "/Library/Kontakt.vst3", false),
                installed("Kontakt", "NI", "/Library/Kontakt.vst3", true)});

    const auto match =
        magda::matchInstalledPlugin(saved("Kontakt", "NI", "/Library/Kontakt.vst3", true), list);

    REQUIRE(match.found);
    CHECK(match.description.isInstrument);
}

TEST_CASE("A plugin that moved is found by name and vendor", "[plugins][lookup]") {
    juce::KnownPluginList list;
    fill(list, {installed("Pro-Q 3", "FabFilter", "/new/Pro-Q 3.vst3", false)});

    const auto match = magda::matchInstalledPlugin(
        saved("Pro-Q 3", "FabFilter", "/old/Pro-Q 3.vst3", false), list);

    REQUIRE(match.found);
    CHECK(match.description.fileOrIdentifier == "/new/Pro-Q 3.vst3");
}

TEST_CASE("A moved plugin is not resolved to the same plugin in another format",
          "[plugins][lookup]") {
    // The common macOS case: the same plugin installed twice, as an AU and as a
    // VST3, with the same name, vendor and role. The saved VST3 has moved, so
    // the file pass cannot answer, and without the format in the vendor pass
    // the AU wins by being there. It is not a substitute: its parameter layout
    // and its state are its own, so the project would load and be wrong.
    juce::KnownPluginList list;
    fill(list, {installed("Pro-Q 3", "FabFilter", "/Library/ProQ.component", false, "AudioUnit")});

    const auto match =
        magda::matchInstalledPlugin(saved("Pro-Q 3", "FabFilter", "/old/ProQ.vst3", false), list);

    CHECK_FALSE(match.found);
}

TEST_CASE("A plugin renamed in place is found by its file alone", "[plugins][lookup]") {
    // The pass the rack path did not have before this was one function: a
    // plugin that was renamed by its vendor resolved on a track and not inside
    // a rack, in the same project, on the same machine.
    juce::KnownPluginList list;
    fill(list, {installed("Pro-Q 4", "FabFilter", "/Library/ProQ.vst3", false)});

    const auto match = magda::matchInstalledPlugin(
        saved("Pro-Q 3", "FabFilter", "/Library/ProQ.vst3", false), list);

    REQUIRE(match.found);
    CHECK(match.description.name == "Pro-Q 4");
}

TEST_CASE("A project from another host resolves by name and format", "[plugins][lookup]") {
    // A DAWproject carries the name and the class id and neither our file path
    // nor a reliable role: Bitwig exports Serum, an instrument, as an audio
    // effect. So this pass can require neither the vendor nor the flag.
    juce::KnownPluginList list;
    fill(list, {installed("Serum", "Xfer Records", "/Library/Serum.vst3", true)});

    auto imported = saved("Serum", "", "", false);
    const auto match = magda::matchInstalledPlugin(imported, list);

    REQUIRE(match.found);
    CHECK(match.description.isInstrument);
    CHECK(match.description.manufacturerName == "Xfer Records");
}

TEST_CASE("Plugin assignment generations survive snapshots and explicitly change for replacements",
          "[plugins][lookup]") {
    auto assignment = saved("Kontakt", "NI", "/Library/Kontakt.vst3", true);
    const auto copy = assignment;
    const auto originalGeneration = assignment.pluginAssignmentGeneration;

    // Production replacement paths mutate an existing DeviceInfo or copy a
    // browser template, so replacement identity is authored explicitly rather
    // than inferred from C++ object construction.
    assignment.beginNewPluginAssignment();
    assignment.isInstrument = false;

    CHECK(copy.pluginAssignmentGeneration == originalGeneration);
    CHECK(assignment.pluginAssignmentGeneration > originalGeneration);
}

TEST_CASE("The format is part of the last pass", "[plugins][lookup]") {
    // Same name, different format. An AU is not a substitute for the VST3 a
    // project saved: the two have different parameter lists and different
    // state, so the project would load and be wrong.
    juce::KnownPluginList list;
    fill(list, {installed("Serum", "Xfer Records", "/Library/Serum.component", true, "AudioUnit")});

    const auto match = magda::matchInstalledPlugin(saved("Serum", "", "", true), list);

    CHECK_FALSE(match.found);
}

TEST_CASE("A saved device with no name matches nothing by name", "[plugins][lookup]") {
    // The other half of the same rule as the file guard below. A device with
    // neither a name nor a file reaches the app's lookup now that the call
    // sites no longer refuse to search, and it must not resolve to whichever
    // scanned plugin happens to have an empty name.
    juce::KnownPluginList list;
    fill(list, {installed("", "", "/Library/Nameless.vst3", false)});

    const auto match = magda::matchInstalledPlugin(saved("", "", "", false), list);

    CHECK_FALSE(match.found);
}

TEST_CASE("A saved device with no file does not match a description with none",
          "[plugins][lookup]") {
    // Both file passes ask nothing here. An empty path equalling an empty path
    // is a match on the absence of evidence, and it would resolve every device
    // that saved no file to whichever scanned plugin happens to have none.
    juce::KnownPluginList list;
    fill(list, {installed("Something Else", "Vendor", "", false)});

    const auto match = magda::matchInstalledPlugin(saved("Pro-Q 3", "FabFilter", "", false), list);

    CHECK_FALSE(match.found);
}

TEST_CASE("An unfound plugin comes back as what the project saved", "[plugins][lookup]") {
    juce::KnownPluginList list;
    fill(list, {installed("Pro-Q 3", "FabFilter", "/Library/ProQ.vst3", false)});

    const auto match =
        magda::matchInstalledPlugin(saved("Massive X", "NI", "/Library/MassiveX.vst3", true), list);

    REQUIRE_FALSE(match.found);

    // Not empty: the saved fields, as a description. A session is free to try
    // to load it and let the format's own lookup have a go, which is what the
    // app does and what lets a plugin resolve when the scan is out of date.
    CHECK(match.description.name == "Massive X");
    CHECK(match.description.fileOrIdentifier == "/Library/MassiveX.vst3");
    CHECK(match.description.pluginFormatName == "VST3");
}

TEST_CASE("An empty list finds nothing and says so", "[plugins][lookup]") {
    const juce::KnownPluginList empty;

    const auto match = magda::matchInstalledPlugin(
        saved("Pro-Q 3", "FabFilter", "/Library/ProQ.vst3", false), empty);

    CHECK_FALSE(match.found);
}
