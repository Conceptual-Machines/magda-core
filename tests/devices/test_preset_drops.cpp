// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>
#include <memory>

#include "StructuralRoundTrip.hpp"
#include "magda/daw/core/AppPaths.hpp"
#include "magda/daw/core/Config.hpp"
#include "magda/daw/core/PluginPresetScanner.hpp"
#include "magda/daw/core/PresetDrops.hpp"
#include "magda/daw/core/PresetManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

// Presets dropped on a track's chain or a device slot (#1329): rack presets
// insert a fresh rack where they land, and a plugin's own preset files load
// into the device they are dropped on, when they are that plugin's.

namespace {

using namespace magda;
using magda::structural_test::resetState;

/** @brief Points the presets folder at a fresh temporary one for the test's duration. */
struct PresetDirectoryScope {
    PresetDirectoryScope()
        : previous(Config::getInstance().getPresetsDir()),
          directory(juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getNonexistentChildFile("magda-preset-drops", {})) {
        REQUIRE(directory.createDirectory());
        Config::getInstance().setPresetsDir(directory.getFullPathName().toStdString());
        paths::resolve();
    }

    ~PresetDirectoryScope() {
        Config::getInstance().setPresetsDir(previous);
        paths::resolve();
        directory.deleteRecursively();
    }

    std::string previous;
    juce::File directory;
};

DeviceInfo effect(const juce::String& name) {
    DeviceInfo device;
    device.name = name;
    device.pluginId = "delay";
    device.format = PluginFormat::Internal;
    return device;
}

const RackInfo* rackAt(TrackId trackId, size_t index) {
    const auto& elements = TrackManager::getInstance().getChainElements(trackId);
    if (index >= elements.size())
        return nullptr;
    const auto* rack = std::get_if<std::unique_ptr<RackInfo>>(&elements[index]);
    return rack != nullptr ? rack->get() : nullptr;
}

/// A .vstpreset header naming @p classId: magic, version, class id, chunk-list offset.
void writeVstPreset(const juce::File& file, const juce::String& classId) {
    juce::MemoryOutputStream out;
    out.write("VST3", 4);
    out.writeInt(1);
    out.write(classId.toRawUTF8(), 32);
    out.writeInt64(48);
    REQUIRE(file.replaceWithData(out.getData(), out.getDataSize()));
}

int fourCC(const char* code) {
    return (code[0] << 24) | (code[1] << 16) | (code[2] << 8) | code[3];
}

/// An XML .aupreset naming the AU it was saved from.
void writeAuPreset(const juce::File& file, const char* subtype, const char* manufacturer) {
    const juce::String plist =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\"><dict>"
        "<key>manufacturer</key><integer>" +
        juce::String(fourCC(manufacturer)) +
        "</integer><key>name</key><string>Test</string><key>subtype</key><integer>" +
        juce::String(fourCC(subtype)) + "</integer><key>type</key><integer>" +
        juce::String(fourCC("aumu")) + "</integer></dict></plist>\n";
    REQUIRE(file.replaceWithText(plist));
}

DeviceInfo hosted(PluginFormat format) {
    DeviceInfo device;
    device.name = "Hosted";
    device.format = format;
    return device;
}

}  // namespace

TEST_CASE("A dropped rack preset inserts a fresh rack where it lands", "[presets][drop]") {
    PresetDirectoryScope scope;
    resetState();
    auto& tm = TrackManager::getInstance();
    auto& presets = PresetManager::getInstance();

    const auto trackId = tm.createTrack("Track");
    tm.addDeviceToTrack(trackId, effect("Resident"));

    RackInfo rack;
    rack.name = "Dropped";
    ChainInfo chain;
    chain.name = "Chain 1";
    chain.elements.push_back(makeDeviceElement(effect("Inside")));
    rack.chains.push_back(std::move(chain));
    REQUIRE(presets.saveRackPreset(rack, "Dropped"));

    const auto ref = presets.classifyPresetFile(
        presets.getRacksDirectory().getChildFile("Dropped").withFileExtension(".mps"));
    REQUIRE(ref.has_value());
    REQUIRE(ref->kind == PresetManager::PresetRef::Kind::Rack);

    RackInfo loaded;
    REQUIRE(presets.loadRackPreset(ref->name, loaded));

    // Twice at the front: the same preset has to make two independent racks.
    REQUIRE(insertRackPreset(trackId, loaded, 0));
    REQUIRE(insertRackPreset(trackId, loaded, 0));
    REQUIRE(tm.getChainElements(trackId).size() == 3);

    const auto* first = rackAt(trackId, 0);
    const auto* second = rackAt(trackId, 1);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first->name == "Dropped");
    CHECK(first->id != second->id);
    REQUIRE(first->chains.size() == 1);
    CHECK(first->chains.front().elements.size() == 1);

    // One drop is one undo step.
    REQUIRE(UndoManager::getInstance().undo());
    CHECK(tm.getChainElements(trackId).size() == 2);
}

TEST_CASE("A plugin's preset file fits only its own plugin", "[presets][drop]") {
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getNonexistentChildFile("magda-native-presets", {});
    REQUIRE(folder.createDirectory());

    SECTION("VST3, by the class id in its header") {
        const juce::String ours = "0123456789ABCDEF0123456789ABCDEF";
        const auto file = folder.getChildFile("Patch.vstpreset");
        writeVstPreset(file, ours);

        auto device = hosted(PluginFormat::VST3);
        device.vst3ClassId = ours.toLowerCase();
        CHECK(PluginPresetScanner::presetFitsDevice(file, device));

        device.vst3ClassId = "FEDCBA9876543210FEDCBA9876543210";
        CHECK_FALSE(PluginPresetScanner::presetFitsDevice(file, device));

        // Not yet reported: the plugin's own loader checks the class.
        device.vst3ClassId.clear();
        CHECK(PluginPresetScanner::presetFitsDevice(file, device));
    }

    SECTION("AU, by the subtype and manufacturer in its plist") {
        const auto file = folder.getChildFile("Patch.aupreset");
        writeAuPreset(file, "Smpl", "appl");

        auto device = hosted(PluginFormat::AU);
        device.fileOrIdentifier = "AudioUnit:Synths/aumu,Smpl,appl";
        CHECK(PluginPresetScanner::presetFitsDevice(file, device));

        device.fileOrIdentifier = "AudioUnit:Synths/aumu,Othr,appl";
        CHECK_FALSE(PluginPresetScanner::presetFitsDevice(file, device));

        // A binary plist names nothing readable, so the plugin decides.
        const auto binary = folder.getChildFile("Binary.aupreset");
        REQUIRE(binary.replaceWithText("bplist00"));
        CHECK(PluginPresetScanner::presetFitsDevice(binary, device));
    }

    SECTION("only in the device's own format") {
        const auto vst = folder.getChildFile("Patch.vstpreset");
        writeVstPreset(vst, "0123456789ABCDEF0123456789ABCDEF");
        CHECK_FALSE(PluginPresetScanner::presetFitsDevice(vst, hosted(PluginFormat::AU)));
        CHECK_FALSE(PluginPresetScanner::presetFitsDevice(vst, effect("Built in")));
        CHECK_FALSE(PluginPresetScanner::presetFitsDevice(folder.getChildFile("Missing.vstpreset"),
                                                          hosted(PluginFormat::VST3)));
    }

    folder.deleteRecursively();
}
