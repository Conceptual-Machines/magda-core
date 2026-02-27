#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/project/ProjectManager.hpp"
#include "magda/daw/project/serialization/PluginStateCodec.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"

using namespace magda;

// Re-use the same fixture pattern from test_project_serialization.cpp
struct PluginStateFixture {
    std::vector<juce::File> tempFiles;

    PluginStateFixture() {
        TrackManager::getInstance().clearAllTracks();
        ClipManager::getInstance().clearAllClips();
        AutomationManager::getInstance().clearAll();
    }

    ~PluginStateFixture() {
        for (auto& file : tempFiles) {
            if (file.existsAsFile())
                file.deleteFile();
        }
        TrackManager::getInstance().clearAllTracks();
        ClipManager::getInstance().clearAllClips();
        AutomationManager::getInstance().clearAll();
    }

    juce::File createTempFile(const juce::String& suffix) {
        auto file = juce::File::createTempFile(suffix);
        tempFiles.push_back(file);
        return file;
    }
};

// ============================================================================
// PluginStateCodec unit tests
// ============================================================================

TEST_CASE("PluginStateCodec encode/decode roundtrip", "[project][serialization][pluginstate]") {
    SECTION("Empty ValueTree returns empty string") {
        juce::ValueTree empty;
        auto encoded = PluginStateCodec::encode(empty);
        REQUIRE(encoded.isEmpty());
    }

    SECTION("Decode empty string returns invalid ValueTree") {
        auto decoded = PluginStateCodec::decode("");
        REQUIRE(!decoded.isValid());
    }

    SECTION("Simple ValueTree roundtrip") {
        juce::ValueTree tree("PLUGIN");
        tree.setProperty("type", "TestPlugin", nullptr);
        tree.setProperty("gain", 0.75f, nullptr);
        tree.setProperty("enabled", true, nullptr);

        auto encoded = PluginStateCodec::encode(tree);
        REQUIRE(encoded.isNotEmpty());

        auto decoded = PluginStateCodec::decode(encoded);
        REQUIRE(decoded.isValid());
        REQUIRE(decoded.getType().toString() == "PLUGIN");
        REQUIRE(static_cast<float>(decoded.getProperty("gain")) == 0.75f);
        REQUIRE(static_cast<bool>(decoded.getProperty("enabled")) == true);
        REQUIRE(decoded.getProperty("type").toString() == "TestPlugin");
    }

    SECTION("ValueTree with child nodes roundtrip") {
        juce::ValueTree tree("PLUGIN");
        tree.setProperty("type", "ExternalPlugin", nullptr);

        juce::ValueTree param1("PARAM");
        param1.setProperty("name", "Cutoff", nullptr);
        param1.setProperty("value", 0.5f, nullptr);
        tree.addChild(param1, -1, nullptr);

        juce::ValueTree param2("PARAM");
        param2.setProperty("name", "Resonance", nullptr);
        param2.setProperty("value", 0.3f, nullptr);
        tree.addChild(param2, -1, nullptr);

        auto encoded = PluginStateCodec::encode(tree);
        REQUIRE(encoded.isNotEmpty());

        auto decoded = PluginStateCodec::decode(encoded);
        REQUIRE(decoded.isValid());
        REQUIRE(decoded.getNumChildren() == 2);
        REQUIRE(decoded.getChild(0).getProperty("name").toString() == "Cutoff");
        REQUIRE(static_cast<float>(decoded.getChild(0).getProperty("value")) == 0.5f);
        REQUIRE(decoded.getChild(1).getProperty("name").toString() == "Resonance");
        REQUIRE(static_cast<float>(decoded.getChild(1).getProperty("value")) == 0.3f);
    }

    SECTION("Large ValueTree with many properties compresses well") {
        juce::ValueTree tree("PLUGIN");
        tree.setProperty("type", "LargePlugin", nullptr);

        // Simulate a plugin with many parameters
        for (int i = 0; i < 200; ++i) {
            juce::ValueTree param("PARAM");
            param.setProperty("name", "Parameter_" + juce::String(i), nullptr);
            param.setProperty("value", static_cast<float>(i) / 200.0f, nullptr);
            param.setProperty("min", 0.0f, nullptr);
            param.setProperty("max", 1.0f, nullptr);
            tree.addChild(param, -1, nullptr);
        }

        auto encoded = PluginStateCodec::encode(tree);
        REQUIRE(encoded.isNotEmpty());

        // The compressed base64 should be significantly smaller than the raw XML
        auto xml = tree.createXml();
        auto rawXmlSize = xml->toString().getNumBytesAsUTF8();
        auto compressedSize = encoded.getNumBytesAsUTF8();
        REQUIRE(compressedSize < rawXmlSize);

        // Roundtrip
        auto decoded = PluginStateCodec::decode(encoded);
        REQUIRE(decoded.isValid());
        REQUIRE(decoded.getNumChildren() == 200);
        REQUIRE(decoded.getChild(99).getProperty("name").toString() == "Parameter_99");
    }

    SECTION("Decode with corrupted base64 returns invalid ValueTree") {
        auto decoded = PluginStateCodec::decode("!!!not-valid-base64!!!");
        REQUIRE(!decoded.isValid());
    }

    SECTION("ValueTree with special characters roundtrip") {
        juce::ValueTree tree("PLUGIN");
        tree.setProperty("name", "Plugin with <special> & \"chars\"", nullptr);
        tree.setProperty("path", "/usr/lib/plugins/my plugin.vst3", nullptr);

        auto encoded = PluginStateCodec::encode(tree);
        auto decoded = PluginStateCodec::decode(encoded);
        REQUIRE(decoded.isValid());
        REQUIRE(decoded.getProperty("name").toString() == "Plugin with <special> & \"chars\"");
        REQUIRE(decoded.getProperty("path").toString() == "/usr/lib/plugins/my plugin.vst3");
    }
}

// ============================================================================
// DeviceInfo serialization with plugin state blob
// ============================================================================

TEST_CASE("DeviceInfo plugin state serialization", "[project][serialization][pluginstate]") {
    PluginStateFixture fixture;

    SECTION("Full save/load roundtrip with plugin state blob") {
        auto& projectManager = ProjectManager::getInstance();
        auto& trackManager = TrackManager::getInstance();

        // Create a track with a device that has plugin state
        auto trackId = trackManager.createTrack("Synth Track", TrackType::MIDI);

        DeviceInfo device;
        device.id = 1;
        device.name = "FabFilter Pro-Q 3";
        device.pluginId = "FabFilter Pro-Q 3";
        device.manufacturer = "FabFilter";
        device.format = PluginFormat::VST3;
        device.isInstrument = false;
        device.bypassed = false;

        // Simulate a realistic plugin state tree
        juce::ValueTree pluginTree("PLUGIN");
        pluginTree.setProperty("type", "vst3", nullptr);
        pluginTree.setProperty("uid", "AABBCCDD", nullptr);

        for (int i = 0; i < 24; ++i) {
            juce::ValueTree band("BAND");
            band.setProperty("index", i, nullptr);
            band.setProperty("frequency", 100.0f * static_cast<float>(i + 1), nullptr);
            band.setProperty("gain", (i % 2 == 0) ? 3.0f : -3.0f, nullptr);
            band.setProperty("q", 1.0f, nullptr);
            band.setProperty("enabled", i < 8, nullptr);
            pluginTree.addChild(band, -1, nullptr);
        }

        device.pluginStateData = PluginStateCodec::encode(pluginTree);
        trackManager.addDeviceToTrack(trackId, device);

        // Save the project
        auto tempFile = fixture.createTempFile(".mgd");
        bool saved = projectManager.saveProjectAs(tempFile);
        REQUIRE(saved);

        // Clear everything
        trackManager.clearAllTracks();
        REQUIRE(trackManager.getTracks().empty());

        // Load back
        bool loaded = projectManager.loadProject(tempFile);
        REQUIRE(loaded);

        // Verify the track and device were restored
        const auto& tracks = trackManager.getTracks();
        REQUIRE(tracks.size() == 1);
        REQUIRE(tracks[0].chainElements.size() == 1);
        REQUIRE(isDevice(tracks[0].chainElements[0]));

        const auto& restoredDevice = getDevice(tracks[0].chainElements[0]);
        REQUIRE(restoredDevice.name == "FabFilter Pro-Q 3");

        // Verify plugin state blob was preserved
        REQUIRE(restoredDevice.pluginStateData.isNotEmpty());

        // Decode and verify contents
        auto restoredTree = PluginStateCodec::decode(restoredDevice.pluginStateData);
        REQUIRE(restoredTree.isValid());
        REQUIRE(restoredTree.getType().toString() == "PLUGIN");
        REQUIRE(restoredTree.getProperty("uid").toString() == "AABBCCDD");
        REQUIRE(restoredTree.getNumChildren() == 24);

        // Spot-check a couple of bands
        auto band0 = restoredTree.getChild(0);
        REQUIRE(static_cast<float>(band0.getProperty("frequency")) == 100.0f);
        REQUIRE(static_cast<float>(band0.getProperty("gain")) == 3.0f);
        REQUIRE(static_cast<bool>(band0.getProperty("enabled")) == true);

        auto band8 = restoredTree.getChild(8);
        REQUIRE(static_cast<bool>(band8.getProperty("enabled")) == false);
    }

    SECTION("Device without plugin state blob (backward compatibility)") {
        auto& projectManager = ProjectManager::getInstance();
        auto& trackManager = TrackManager::getInstance();

        // Create a track with a device that has NO plugin state (simulates old project)
        auto trackId = trackManager.createTrack("FX Track", TrackType::Audio);

        DeviceInfo device;
        device.id = 1;
        device.name = "Delay";
        device.pluginId = "delay";
        device.format = PluginFormat::Internal;
        // pluginStateData left empty (default)

        trackManager.addDeviceToTrack(trackId, device);

        // Save
        auto tempFile = fixture.createTempFile(".mgd");
        bool saved = projectManager.saveProjectAs(tempFile);
        REQUIRE(saved);

        // Clear and load
        trackManager.clearAllTracks();
        bool loaded = projectManager.loadProject(tempFile);
        REQUIRE(loaded);

        // Verify device restored with empty plugin state
        const auto& tracks = trackManager.getTracks();
        REQUIRE(tracks.size() == 1);
        REQUIRE(tracks[0].chainElements.size() == 1);
        const auto& restoredDevice = getDevice(tracks[0].chainElements[0]);
        REQUIRE(restoredDevice.pluginStateData.isEmpty());
    }

    SECTION("Multiple devices with different plugin states on same track") {
        auto& projectManager = ProjectManager::getInstance();
        auto& trackManager = TrackManager::getInstance();

        auto trackId = trackManager.createTrack("Multi-FX Track", TrackType::Audio);

        // Device 1 - with state
        DeviceInfo device1;
        device1.id = 1;
        device1.name = "EQ";
        device1.pluginId = "eq";
        device1.format = PluginFormat::Internal;

        juce::ValueTree eqState("PLUGIN");
        eqState.setProperty("lowCut", 80.0f, nullptr);
        eqState.setProperty("highCut", 18000.0f, nullptr);
        device1.pluginStateData = PluginStateCodec::encode(eqState);

        trackManager.addDeviceToTrack(trackId, device1);

        // Device 2 - with different state
        DeviceInfo device2;
        device2.id = 2;
        device2.name = "Compressor";
        device2.pluginId = "compressor";
        device2.format = PluginFormat::Internal;

        juce::ValueTree compState("PLUGIN");
        compState.setProperty("threshold", -18.0f, nullptr);
        compState.setProperty("ratio", 4.0f, nullptr);
        compState.setProperty("attack", 10.0f, nullptr);
        compState.setProperty("release", 100.0f, nullptr);
        device2.pluginStateData = PluginStateCodec::encode(compState);

        trackManager.addDeviceToTrack(trackId, device2);

        // Device 3 - no state
        DeviceInfo device3;
        device3.id = 3;
        device3.name = "Reverb";
        device3.pluginId = "reverb";
        device3.format = PluginFormat::Internal;
        // No pluginStateData

        trackManager.addDeviceToTrack(trackId, device3);

        // Save / clear / load
        auto tempFile = fixture.createTempFile(".mgd");
        REQUIRE(projectManager.saveProjectAs(tempFile));

        trackManager.clearAllTracks();
        REQUIRE(projectManager.loadProject(tempFile));

        // Verify all three devices
        const auto& tracks = trackManager.getTracks();
        REQUIRE(tracks.size() == 1);
        REQUIRE(tracks[0].chainElements.size() == 3);

        // Device 1 - EQ state
        const auto& d1 = getDevice(tracks[0].chainElements[0]);
        REQUIRE(d1.pluginStateData.isNotEmpty());
        auto tree1 = PluginStateCodec::decode(d1.pluginStateData);
        REQUIRE(static_cast<float>(tree1.getProperty("lowCut")) == 80.0f);

        // Device 2 - Compressor state
        const auto& d2 = getDevice(tracks[0].chainElements[1]);
        REQUIRE(d2.pluginStateData.isNotEmpty());
        auto tree2 = PluginStateCodec::decode(d2.pluginStateData);
        REQUIRE(static_cast<float>(tree2.getProperty("threshold")) == -18.0f);
        REQUIRE(static_cast<float>(tree2.getProperty("ratio")) == 4.0f);

        // Device 3 - no state
        const auto& d3 = getDevice(tracks[0].chainElements[2]);
        REQUIRE(d3.pluginStateData.isEmpty());
    }
}
