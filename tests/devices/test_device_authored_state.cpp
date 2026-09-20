#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>

#include "../../magda/daw/audio/plugins/OscilloscopePlugin.hpp"
#include "../../magda/daw/audio/plugins/SpectrumAnalyzerPlugin.hpp"
#include "../../magda/daw/core/DeviceState.hpp"
#include "../../magda/daw/core/DeviceStateCommands.hpp"
#include "../../magda/daw/core/TrackManager.hpp"
#include "../../magda/daw/core/UndoManager.hpp"
#include "../../magda/daw/engine/PluginService.hpp"
#include "../../magda/daw/engine/TracktionEngineWrapper.hpp"

using namespace magda;
namespace ds = magda::device_state;

// ============================================================================
// #2317 — authored-state edits on the model. No live engine in this binary:
// TrackManager has no audio engine, so the projection is skipped and what is
// under test is exactly the model-side contract.
// ============================================================================

namespace {

class RenderedDeviceEngine final : public TracktionEngineWrapper {
  public:
    RenderedDeviceEngine()
        : rendered(std::make_shared<daw::audio::OscilloscopePlugin>(
              daw::audio::DevicePluginDefaults::Oscilloscope{})) {}

    AudioBridge* getAudioBridge() override {
        return nullptr;
    }
    const AudioBridge* getAudioBridge() const override {
        return nullptr;
    }
    std::shared_ptr<daw::audio::MagdaDevice> renderedDevice(const ChainNodePath&) const override {
        return rendered;
    }
    void captureAllPluginStates() override {
        ++captureAllCalls;
    }
    void capturePluginStateAt(const ChainNodePath&) override {
        ++captureOneCalls;
    }
    void applyPluginStateAt(const ChainNodePath&) override {
        ++applyOneCalls;
    }

    std::shared_ptr<daw::audio::OscilloscopePlugin> rendered;
    int captureAllCalls = 0;
    int captureOneCalls = 0;
    int applyOneCalls = 0;
};

ChainNodePath addInternalDevice(const juce::String& pluginId, const juce::String& pluginState) {
    auto& tracks = TrackManager::getInstance();
    tracks.clearAllTracks();
    UndoManager::getInstance().clearHistory();
    const auto trackId = tracks.createTrack("Authored", TrackType::Media);

    DeviceInfo device;
    device.name = pluginId;
    device.pluginId = pluginId;
    device.format = PluginFormat::Internal;
    device.pluginState = pluginState;
    const auto deviceId = tracks.addDeviceToTrack(trackId, device);
    REQUIRE(deviceId != INVALID_DEVICE_ID);
    return ChainNodePath::topLevelDevice(trackId, deviceId);
}

}  // namespace

TEST_CASE("An authored-state edit never re-encodes the retired parameter record",
          "[device-authored-state]") {
    // A pre-#2317 document still carrying its duplicate `params`. The edit is
    // the moment the document goes canonical: hydration consumed the record at
    // load, and writing it back would leave a second persisted authority alive
    // in every path that never passes through a Tracktion capture.
    ds::Doc oldDoc;
    oldDoc.deviceType = "arpeggiator";
    oldDoc.paramsAreDisplayDomain = true;
    oldDoc.params.push_back({0, "pattern", 2.0f});
    oldDoc.params.push_back({3, "gate", 0.8f});
    oldDoc.root.props.set(juce::Identifier("arpQuantizeSub"), 16);

    const auto path = addInternalDevice("arpeggiator", ds::encode(oldDoc));

    static const juce::Identifier quantizeSub("arpQuantizeSub");
    REQUIRE(TrackManager::getInstance().updateDeviceAuthoredState(
        path, [](ds::Doc& doc) { doc.root.props.set(quantizeSub, 8); }));

    const auto* device = TrackManager::getInstance().getDeviceInChainByPath(path);
    REQUIRE(device != nullptr);
    const auto written = ds::decode(device->pluginState);
    REQUIRE(written.has_value());
    CHECK(written->params.empty());
    CHECK_FALSE(written->paramsAreDisplayDomain);
    CHECK(static_cast<int>(written->root.props[quantizeSub]) == 8);
    CHECK(written->deviceType == "arpeggiator");

    TrackManager::getInstance().clearAllTracks();
}

TEST_CASE("Undoing an authored-state command cannot resurrect the retired record",
          "[device-authored-state]") {
    // The command snapshots DeviceInfo::pluginState verbatim, and a device
    // that has not been saved since #2317 still holds a document WITH the
    // retired `params`. The canonicalization therefore lives at the write
    // boundary (setDeviceAuthoredState), not in the forward edit alone:
    // execute -> undo must leave a canonical document, not the pre-#2317 one.
    ds::Doc oldDoc;
    oldDoc.deviceType = "magda_convolution";
    oldDoc.params.push_back({0, "gain", 0.5f});
    oldDoc.root.props.set(juce::Identifier("normalise"), true);

    const auto path = addInternalDevice("magda_convolution", ds::encode(oldDoc));

    juce::MemoryBlock blob;
    blob.append("notrealaudio", 12);
    LoadImpulseResponseCommand command(path, "Test IR", std::move(blob));
    REQUIRE(command.canExecute());
    command.execute();
    REQUIRE(command.wasExecuted());

    auto& tracks = TrackManager::getInstance();
    const auto* device = tracks.getDeviceInChainByPath(path);
    REQUIRE(device != nullptr);
    {
        const auto written = ds::decode(device->pluginState);
        REQUIRE(written.has_value());
        CHECK(written->params.empty());
        CHECK(written->root.props.contains(juce::Identifier("irFileData")));
    }

    command.undo();
    {
        const auto restored = ds::decode(device->pluginState);
        REQUIRE(restored.has_value());
        CHECK(restored->params.empty());
        CHECK_FALSE(restored->root.props.contains(juce::Identifier("irFileData")));
        // The authored state itself came back with the snapshot.
        CHECK(static_cast<bool>(restored->root.props[juce::Identifier("normalise")]));
    }

    tracks.clearAllTracks();
}

TEST_CASE("Snapshot replacement validates the incoming state too", "[device-authored-state]") {
    auto& tracks = TrackManager::getInstance();

    SECTION("a future-schema snapshot is refused, not stored unreadable") {
        const auto path = addInternalDevice("arpeggiator", {});
        const juce::String futureDoc =
            "{\"schema\": 99, \"device\": \"arpeggiator\", \"somethingNewer\": true}";
        CHECK_FALSE(tracks.setDeviceAuthoredState(path, futureDoc));
        const auto* device = tracks.getDeviceInChainByPath(path);
        REQUIRE(device != nullptr);
        CHECK(device->pluginState.isEmpty());
        tracks.clearAllTracks();
    }

    SECTION("another device's document is refused") {
        const auto path = addInternalDevice("arpeggiator", {});
        ds::Doc wrongDevice;
        wrongDevice.deviceType = "magda_convolution";
        wrongDevice.root.props.set(juce::Identifier("normalise"), true);
        CHECK_FALSE(tracks.setDeviceAuthoredState(path, ds::encode(wrongDevice)));
        const auto* device = tracks.getDeviceInChainByPath(path);
        REQUIRE(device != nullptr);
        CHECK(device->pluginState.isEmpty());
        tracks.clearAllTracks();
    }
}

TEST_CASE("Snapshot replacement passes through what decode refuses", "[device-authored-state]") {
    // Legacy engine XML is not a v2 document; a snapshot of it restores
    // verbatim rather than being mangled into one.
    const juce::String legacyXml = "<PLUGIN type=\"arpeggiator\" arpQuantizeSub=\"8\"/>";
    const auto path = addInternalDevice("arpeggiator", {});
    REQUIRE(TrackManager::getInstance().setDeviceAuthoredState(path, legacyXml));
    const auto* device = TrackManager::getInstance().getDeviceInChainByPath(path);
    REQUIRE(device != nullptr);
    CHECK(device->pluginState == legacyXml);
    TrackManager::getInstance().clearAllTracks();
}

TEST_CASE("Authored-state edits refuse what they cannot read", "[device-authored-state]") {
    const juce::String futureDoc =
        "{\"schema\": 99, \"device\": \"arpeggiator\", \"somethingNewer\": true}";

    SECTION("updateDeviceAuthoredState leaves a future-schema document untouched") {
        const auto path = addInternalDevice("arpeggiator", futureDoc);
        CHECK_FALSE(TrackManager::getInstance().updateDeviceAuthoredState(
            path, [](ds::Doc& doc) { doc.root.props.set(juce::Identifier("x"), 1); }));
        const auto* device = TrackManager::getInstance().getDeviceInChainByPath(path);
        REQUIRE(device != nullptr);
        CHECK(device->pluginState == futureDoc);
        TrackManager::getInstance().clearAllTracks();
    }

    SECTION("LoadImpulseResponseCommand::canExecute rejects future-schema state") {
        const auto path = addInternalDevice("magda_convolution", futureDoc);
        juce::MemoryBlock blob;
        blob.append("x", 1);
        LoadImpulseResponseCommand command(path, "Some IR", std::move(blob));
        CHECK_FALSE(command.canExecute());
        TrackManager::getInstance().clearAllTracks();
    }

    SECTION("LoadImpulseResponseCommand::canExecute rejects a non-convolution device") {
        const auto path = addInternalDevice("arpeggiator", {});
        juce::MemoryBlock blob;
        blob.append("x", 1);
        LoadImpulseResponseCommand command(path, "Some IR", std::move(blob));
        CHECK_FALSE(command.canExecute());
        TrackManager::getInstance().clearAllTracks();
    }
}

// ============================================================================
// #2663 — an analyser's Time, Color and FFT are authored state like every other
// faceplate's settings. They used to live only on the live device, where the
// native engine never captured them back and a save lost them.
// ============================================================================

TEST_CASE("An analyser's settings are written to its document", "[device-authored-state][2663]") {
    const auto path = addInternalDevice("oscilloscope", {});

    juce::NamedValueSet settings;
    settings.set("timebaseMs", 250.0f);
    settings.set("traceColour", 3);
    REQUIRE(writeDeviceSettings(path, settings));

    const auto* device = TrackManager::getInstance().getDeviceInChainByPath(path);
    REQUIRE(device != nullptr);
    const auto written = ds::decode(device->pluginState);
    REQUIRE(written.has_value());
    CHECK(static_cast<float>(written->root.props[juce::Identifier("timebaseMs")]) ==
          Catch::Approx(250.0f));
    CHECK(static_cast<int>(written->root.props[juce::Identifier("traceColour")]) == 3);
}

TEST_CASE("The projection puts an analyser's document onto the running device",
          "[device-authored-state][2663]") {
    namespace audio = magda::daw::audio;

    // The whole round trip the faceplate depends on: the keys it writes are the
    // ones the device reads back, through the projection the native engine uses
    // in place of the fork's plugin.
    const auto path = addInternalDevice("oscilloscope", {});

    juce::NamedValueSet scopeSettings;
    scopeSettings.set("timebaseMs", 250.0f);
    scopeSettings.set("traceColour", 3);
    REQUIRE(writeDeviceSettings(path, scopeSettings));

    const auto* saved = TrackManager::getInstance().getDeviceInChainByPath(path);
    REQUIRE(saved != nullptr);

    audio::OscilloscopePlugin scope{audio::DevicePluginDefaults::Oscilloscope{}};
    projectAuthoredStateToDevice(scope, saved->pluginState, "oscilloscope");
    CHECK(scope.timebaseMs() == Catch::Approx(250.0f));
    CHECK(scope.traceColourIndex() == 3);

    const auto spectrumPath = addInternalDevice("spectrumanalyzer", {});

    juce::NamedValueSet spectrumSettings;
    spectrumSettings.set("fftOrder", 12);
    spectrumSettings.set("slopeDbPerOct", 3.0f);
    spectrumSettings.set("smoothing", 0.25f);
    spectrumSettings.set("traceColour", 2);
    REQUIRE(writeDeviceSettings(spectrumPath, spectrumSettings));

    const auto* savedSpectrum = TrackManager::getInstance().getDeviceInChainByPath(spectrumPath);
    REQUIRE(savedSpectrum != nullptr);

    audio::SpectrumAnalyzerPlugin spectrum{audio::DevicePluginDefaults::Spectrum{}};
    projectAuthoredStateToDevice(spectrum, savedSpectrum->pluginState, "spectrumanalyzer");
    CHECK(spectrum.fftOrder() == 12);
    CHECK(spectrum.slopeDbPerOct() == Catch::Approx(3.0f));
    CHECK(spectrum.smoothing() == Catch::Approx(0.25f));
    CHECK(spectrum.traceColourIndex() == 2);
}

TEST_CASE("An internal-device preset is projected onto the native rendered device",
          "[device-authored-state][2663][device-presets]") {
    auto& tracks = TrackManager::getInstance();
    RenderedDeviceEngine engine;
    tracks.setAudioEngine(&engine);

    const auto path = addInternalDevice("oscilloscope", {});
    ds::Doc presetState;
    presetState.deviceType = "oscilloscope";
    presetState.root.props.set(juce::Identifier("timebaseMs"), 250.0f);
    presetState.root.props.set(juce::Identifier("traceColour"), 3);

    const auto* live = tracks.getDeviceInChainByPath(path);
    REQUIRE(live != nullptr);
    auto preset = *live;
    preset.pluginState = ds::encode(presetState);

    REQUIRE(tracks.applyDevicePreset(path, preset));
    CHECK(engine.rendered->timebaseMs() == Catch::Approx(250.0f));
    CHECK(engine.rendered->traceColourIndex() == 3);

    tracks.setAudioEngine(nullptr);
    tracks.clearAllTracks();
}

TEST_CASE("PluginService follows TrackManager's active renderer",
          "[plugin][state-service][test-isolation]") {
    auto& tracks = TrackManager::getInstance();
    auto& plugins = PluginService::getInstance();
    RenderedDeviceEngine engine;
    const auto path = ChainNodePath::topLevelDevice(1, 1);

    tracks.setAudioEngine(&engine);
    plugins.captureAllPluginStates();
    plugins.capturePluginStateAt(path);
    plugins.applyPluginStateAt(path);
    CHECK(engine.captureAllCalls == 1);
    CHECK(engine.captureOneCalls == 1);
    CHECK(engine.applyOneCalls == 1);

    tracks.setAudioEngine(nullptr);
    plugins.captureAllPluginStates();
    plugins.capturePluginStateAt(path);
    plugins.applyPluginStateAt(path);
    CHECK(engine.captureAllCalls == 1);
    CHECK(engine.captureOneCalls == 1);
    CHECK(engine.applyOneCalls == 1);
}
