#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>

#include "magda/daw/api/device_api_live.hpp"
#include "magda/daw/audio/processors/base/DeviceProcessor.hpp"
#include "magda/daw/core/AppPaths.hpp"
#include "magda/daw/core/ChainWalk.hpp"
#include "magda/daw/core/DeviceInfo.hpp"
#include "magda/daw/core/PluginParameterConfigStore.hpp"
#include "magda/daw/core/RackInfo.hpp"
#include "magda/daw/core/TrackManager.hpp"

namespace store = magda::PluginParameterConfigStore;

namespace {

void setEnv(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void unsetEnv(const char* name) {
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

/// Points paths::dataDir at a throwaway directory for the test's lifetime, so
/// the store never touches the real per-user PluginConfigs.
struct TempDataDir {
    TempDataDir() {
        if (const char* value = std::getenv("MAGDA_DATA_DIR"); value != nullptr) {
            previous = juce::String::fromUTF8(value);
            hadPrevious = true;
        }
        dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                  .getChildFile("magda-param-config-store-test");
        dir.deleteRecursively();
        dir.createDirectory();
        setEnv("MAGDA_DATA_DIR", dir.getFullPathName().toRawUTF8());
        magda::paths::resolve();
    }
    ~TempDataDir() {
        if (hadPrevious)
            setEnv("MAGDA_DATA_DIR", previous.toRawUTF8());
        else
            unsetEnv("MAGDA_DATA_DIR");
        magda::paths::resolve();
        dir.deleteRecursively();
    }

    juce::File dir;
    juce::String previous;
    bool hadPrevious = false;
};

magda::DeviceInfo makeExternalDevice() {
    magda::DeviceInfo device;
    device.uniqueId = "VST3-Surge-XT-1a2b3c4d";
    device.format = magda::PluginFormat::VST3;
    device.parameters.emplace_back(0, "Cutoff", "Hz", 20.0f, 20000.0f, 800.0f);
    device.parameters.emplace_back(1, "Resonance", "%", 0.0f, 100.0f, 10.0f);
    device.parameters.emplace_back(2, "Mode", "", 0.0f, 2.0f, 0.0f,
                                   magda::ParameterScale::Discrete);
    device.parameters[2].choices = {"LP", "BP", "HP"};

    // The ids the plugin declares, which is what an entry is matched by.
    device.parameters[0].stableId = "cutoff";
    device.parameters[1].stableId = "res";
    device.parameters[2].stableId = "mode";
    return device;
}

/// A processor whose engine read is a fixed list, standing in for whatever the
/// fork rebuilds a device's parameter array from.
class StubProcessor : public magda::DeviceProcessor {
  public:
    explicit StubProcessor(std::vector<magda::ParameterInfo> parameters)
        : magda::DeviceProcessor(1, nullptr), parameters_(std::move(parameters)) {}

    int getParameterCount() const override {
        return static_cast<int>(parameters_.size());
    }

    magda::ParameterInfo getParameterInfo(int index) const override {
        return parameters_[static_cast<std::size_t>(index)];
    }

  private:
    std::vector<magda::ParameterInfo> parameters_;
};

}  // namespace

TEST_CASE("parameter config save and load round-trip", "[param-config-store]") {
    TempDataDir temp;
    const auto device = makeExternalDevice();

    auto config = store::fromDevice(device);
    config.entries[0].visible = true;
    config.entries[1].visible = true;
    config.entries[1].aiAgent = true;
    config.entries[2].miniMixer = true;
    config.aiPrompt = "Warm analog pads";
    REQUIRE(store::save(device.uniqueId, config));

    const auto loaded = store::load(device.uniqueId);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->pluginId == device.uniqueId);
    REQUIRE(loaded->aiPrompt == "Warm analog pads");
    REQUIRE(loaded->entries.size() == 3);

    REQUIRE(loaded->entries[0].visible);
    REQUIRE_FALSE(loaded->entries[0].aiAgent);
    REQUIRE(loaded->entries[1].aiAgent);
    REQUIRE(loaded->entries[2].miniMixer);

    // Detection data survives the trip.
    REQUIRE(loaded->entries[0].name == "Cutoff");
    REQUIRE(loaded->entries[0].unit.has_value());
    REQUIRE(*loaded->entries[0].unit == "Hz");
    REQUIRE(loaded->entries[0].rangeMin.has_value());
    REQUIRE(*loaded->entries[0].rangeMin == 20.0f);
    REQUIRE(loaded->entries[2].scale.has_value());
    REQUIRE(*loaded->entries[2].scale == magda::ParameterScale::Discrete);
    REQUIRE(loaded->entries[2].choices.has_value());
    REQUIRE(*loaded->entries[2].choices == std::vector<juce::String>{"LP", "BP", "HP"});
}

TEST_CASE("fromDevice describes every parameter, unselected device ticks nothing",
          "[param-config-store]") {
    const auto device = makeExternalDevice();
    const auto config = store::fromDevice(device);

    REQUIRE(config.pluginId == device.uniqueId);
    REQUIRE(config.entries.size() == device.parameters.size());
    for (size_t i = 0; i < config.entries.size(); ++i) {
        REQUIRE(config.entries[i].index == static_cast<int>(i));
        REQUIRE(config.entries[i].name == device.parameters[i].name);
        REQUIRE_FALSE(config.entries[i].visible);
        REQUIRE_FALSE(config.entries[i].miniMixer);
        REQUIRE_FALSE(config.entries[i].aiAgent);
    }
}

TEST_CASE("fromDevice takes the flags off the device's own selections", "[param-config-store]") {
    auto device = makeExternalDevice();
    device.visibleParameters = {0, 2};
    device.miniMixerParameters = {1};
    device.aiSoundDesignerParameters = {0};
    device.aiSoundDesignerPrompt = "Warm analog pads";

    const auto config = store::fromDevice(device);

    REQUIRE(config.aiPrompt == "Warm analog pads");
    REQUIRE(config.entries[0].visible);
    REQUIRE_FALSE(config.entries[1].visible);
    REQUIRE(config.entries[2].visible);
    REQUIRE_FALSE(config.entries[0].miniMixer);
    REQUIRE(config.entries[1].miniMixer);
    REQUIRE(config.entries[0].aiAgent);
    REQUIRE_FALSE(config.entries[1].aiAgent);
    REQUIRE_FALSE(config.entries[2].aiAgent);
}

TEST_CASE("applyToDevice rebuilds selections and applies overrides", "[param-config-store]") {
    TempDataDir temp;
    auto device = makeExternalDevice();

    auto config = store::fromDevice(device);
    config.entries[0].visible = true;
    config.entries[0].aiAgent = true;
    config.entries[0].unit = "kHz";
    config.entries[0].rangeMax = 20.0f;
    config.entries[1].miniMixer = true;
    config.aiPrompt = "Bright leads";
    REQUIRE(store::save(device.uniqueId, config));

    // Stale state on the device must be replaced, not merged.
    device.visibleParameters = {2};
    device.aiSoundDesignerParameters = {2};
    REQUIRE(store::applyToDevice(device.uniqueId, device));

    REQUIRE(device.visibleParameters == std::vector<int>{0});
    REQUIRE(device.miniMixerParameters == std::vector<int>{1});
    REQUIRE(device.aiSoundDesignerParameters == std::vector<int>{0});
    REQUIRE(device.aiSoundDesignerPrompt == "Bright leads");
    REQUIRE(device.parameters[0].unit == "kHz");
    REQUIRE(device.parameters[0].maxValue == 20.0f);

    // No config for this id: the device is left alone.
    magda::DeviceInfo other = makeExternalDevice();
    REQUIRE_FALSE(store::applyToDevice("VST3-Not-Configured", other));
}

TEST_CASE("applyToDevice finds a device's own config file", "[param-config-store][2601]") {
    // The id a config is filed under is the device's, and every caller derived
    // it by hand -- most of them by reading `uniqueId` alone, which is empty on
    // the older devices that carry only a `pluginId`.
    TempDataDir temp;
    auto device = makeExternalDevice();

    auto config = store::fromDevice(device);
    config.entries[0].unit = "kHz";
    REQUIRE(store::save(device.uniqueId, config));

    REQUIRE(store::applyToDevice(device));
    CHECK(device.parameters[0].unit == "kHz");
}

TEST_CASE("A device with no uniqueId is filed under its plugin id", "[param-config-store][2601]") {
    TempDataDir temp;
    auto device = makeExternalDevice();
    device.uniqueId = {};
    device.pluginId = "magda_older_device";

    auto config = store::fromDevice(device);
    config.entries[1].unit = "dB";
    REQUIRE(store::save(device.pluginId, config));

    REQUIRE(store::applyToDevice(device));
    CHECK(device.parameters[1].unit == "dB");
}

TEST_CASE("A config follows its parameter when the plugin renumbers",
          "[param-config-store][2601]") {
    // What a plugin update does: a parameter appears, and everything after it
    // moves down one. Addressed by position, every override past the new one
    // would describe the wrong control from then on.
    TempDataDir temp;
    const auto configured = makeExternalDevice();

    auto config = store::fromDevice(configured);
    config.entries[2].visible = true;
    config.entries[2].unit = "shape";
    REQUIRE(store::save(configured.uniqueId, config));

    auto updated = makeExternalDevice();
    magda::ParameterInfo added(0, "Drive", "", 0.0f, 1.0f, 0.0f);
    added.stableId = "drive";
    updated.parameters.insert(updated.parameters.begin(), added);

    REQUIRE(store::applyToDevice(updated));

    // Mode is at three now, and that is where its override landed. The
    // selection names its slot, which the renumber did not move (#2638).
    CHECK(updated.parameters[3].name == "Mode");
    CHECK(updated.parameters[3].unit == "shape");
    CHECK(updated.visibleParameters == std::vector<int>{2});

    // The parameter that took the old position is untouched.
    CHECK(updated.parameters[2].unit == "%");
}

TEST_CASE("An entry for a parameter the plugin dropped is ignored", "[param-config-store][2601]") {
    TempDataDir temp;
    const auto configured = makeExternalDevice();

    auto config = store::fromDevice(configured);
    config.entries[1].visible = true;
    config.entries[1].unit = "Q";
    REQUIRE(store::save(configured.uniqueId, config));

    auto updated = makeExternalDevice();
    updated.parameters.erase(updated.parameters.begin() + 1);  // Resonance is gone

    REQUIRE(store::applyToDevice(updated));

    // Nothing inherits the missing parameter's override, and Mode -- which now
    // sits where it used to -- keeps its own.
    CHECK(updated.parameters[1].name == "Mode");
    CHECK(updated.parameters[1].unit.isEmpty());
    CHECK(updated.visibleParameters.empty());
}

TEST_CASE("A config written before ids is still addressed by position",
          "[param-config-store][2601]") {
    // Every file on disk today. The id is what a re-save adds.
    TempDataDir temp;
    auto device = makeExternalDevice();

    auto config = store::fromDevice(device);
    for (auto& entry : config.entries)
        entry.id = {};
    config.entries[1].unit = "Q";
    REQUIRE(store::save(device.uniqueId, config));

    REQUIRE(store::applyToDevice(device));
    CHECK(device.parameters[1].unit == "Q");
}

TEST_CASE("A parameter that declares no id keeps its position", "[param-config-store][2601]") {
    TempDataDir temp;
    auto device = makeExternalDevice();
    for (auto& parameter : device.parameters)
        parameter.stableId = {};

    auto config = store::fromDevice(device);
    config.entries[2].unit = "shape";
    REQUIRE(store::save(device.uniqueId, config));

    REQUIRE(store::applyToDevice(device));
    CHECK(device.parameters[2].unit == "shape");
}

TEST_CASE("legacy visible-only config files still load", "[param-config-store]") {
    TempDataDir temp;
    const juce::String uniqueId = "VST3-Legacy-Plugin";

    juce::XmlElement root("ParameterConfig");
    root.setAttribute("pluginId", uniqueId);
    auto* visible = root.createNewChildElement("VisibleParameters");
    visible->createNewChildElement("Param")->setAttribute("index", 1);
    visible->createNewChildElement("Param")->setAttribute("index", 4);
    magda::paths::pluginConfigsDir().createDirectory();
    REQUIRE(root.writeTo(store::configFileFor(uniqueId)));

    const auto loaded = store::load(uniqueId);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->entries.size() == 2);
    REQUIRE(loaded->entries[0].index == 1);
    REQUIRE(loaded->entries[0].visible);
    REQUIRE_FALSE(loaded->entries[0].aiAgent);
    // Legacy entries carry no detection overrides.
    REQUIRE_FALSE(loaded->entries[0].unit.has_value());
    REQUIRE_FALSE(loaded->entries[0].scale.has_value());

    REQUIRE_FALSE(store::hasAiSoundDesignerParameters(uniqueId));
}

TEST_CASE("hasAiSoundDesignerParameters reflects the saved AI selection", "[param-config-store]") {
    TempDataDir temp;
    const auto device = makeExternalDevice();

    auto config = store::fromDevice(device);
    REQUIRE(store::save(device.uniqueId, config));
    REQUIRE_FALSE(store::hasAiSoundDesignerParameters(device.uniqueId));

    config.entries[1].aiAgent = true;
    REQUIRE(store::save(device.uniqueId, config));
    REQUIRE(store::hasAiSoundDesignerParameters(device.uniqueId));
}

TEST_CASE("A rebuilt parameter array carries the plugin's stored config",
          "[param-config-store][2601]") {
    // Both engines rebuild an external plugin's array wholesale on load, so a
    // detected unit that is not re-applied there lasts until the next rebuild
    // and no longer.
    TempDataDir temp;
    const auto configured = makeExternalDevice();

    auto config = store::fromDevice(configured);
    config.entries[0].visible = true;
    config.entries[0].unit = "kHz";
    config.entries[0].rangeMax = 20.0f;
    REQUIRE(store::save(configured.uniqueId, config));

    const StubProcessor processor(configured.parameters);

    magda::DeviceInfo device;
    device.uniqueId = configured.uniqueId;
    device.format = magda::PluginFormat::VST3;
    processor.populateParameters(device, magda::DeviceProcessor::ValueSource::Engine);

    REQUIRE(device.parameters.size() == 3);
    CHECK(device.parameters[0].unit == "kHz");
    CHECK(device.parameters[0].maxValue == 20.0f);
    CHECK(device.visibleParameters == std::vector<int>{0});
}

TEST_CASE("A model-first refresh keeps its values and still gains the config",
          "[param-config-store][2601]") {
    // An internal device takes the other branch of populateParameters: values
    // stay the model's, metadata is re-read, and the overlay goes over both.
    TempDataDir temp;
    const auto configured = makeExternalDevice();

    auto config = store::fromDevice(configured);
    config.entries[1].unit = "Q";
    REQUIRE(store::save("magda_stub_device", config));

    const StubProcessor processor(configured.parameters);

    magda::DeviceInfo device;
    device.pluginId = "magda_stub_device";
    device.format = magda::PluginFormat::Internal;
    device.parameters = configured.parameters;
    device.parameters[1].currentValue = 42.0f;
    processor.populateParameters(device, magda::DeviceProcessor::ValueSource::Model);

    REQUIRE(device.parameters.size() == 3);
    CHECK(device.parameters[1].currentValue == 42.0f);
    CHECK(device.parameters[1].unit == "Q");
}

TEST_CASE("A saved config reaches a live device without a rebuild", "[param-config-store][2601]") {
    // What Configure Parameters leans on. The device is already built, so
    // nothing repopulates its array on save, and since the device slot stopped
    // applying the config itself this is the only thing that puts a new one on
    // a device already on screen.
    TempDataDir temp;
    auto& tm = magda::TrackManager::getInstance();
    tm.clearAllTracks();

    const auto trackId = tm.createTrack("Track");
    tm.addDeviceToTrack(trackId, makeExternalDevice());

    auto config = store::fromDevice(makeExternalDevice());
    config.entries[0].visible = true;
    config.entries[0].unit = "kHz";
    config.entries[1].miniMixer = true;
    REQUIRE(store::save("VST3-Surge-XT-1a2b3c4d", config));

    store::refreshLiveDevices("VST3-Surge-XT-1a2b3c4d");

    const auto* track = tm.getTrack(trackId);
    REQUIRE(track != nullptr);
    REQUIRE(track->chain.fxChainElements.size() == 1);

    const auto& device = magda::getDevice(track->chain.fxChainElements[0]);
    CHECK(device.parameters[0].unit == "kHz");
    CHECK(device.visibleParameters == std::vector<int>{0});
    CHECK(device.miniMixerParameters == std::vector<int>{1});

    tm.clearAllTracks();
}

TEST_CASE("A saved config reaches a device on a Drum Grid pad", "[param-config-store][2601]") {
    // A pad device is configured from the same dialog as any other, and a
    // device is not a leaf when it owns pads (#2204).
    TempDataDir temp;
    auto& tm = magda::TrackManager::getInstance();
    tm.clearAllTracks();

    const auto trackId = tm.createTrack("Track");

    magda::DeviceInfo grid;
    grid.name = "Grid";
    grid.pluginId = "drumgrid";
    grid.format = magda::PluginFormat::Internal;
    grid.isInstrument = true;
    grid.deviceType = magda::DeviceType::Instrument;
    const auto gridId = tm.addDeviceToTrack(trackId, grid);

    const auto gridPath = magda::ChainNodePath::topLevelDevice(trackId, gridId);
    const auto padChainId = tm.ensurePad(gridPath, 0);
    REQUIRE(padChainId != magda::INVALID_CHAIN_ID);
    REQUIRE(tm.addDeviceToPad(gridPath, padChainId, makeExternalDevice()) !=
            magda::INVALID_DEVICE_ID);

    auto config = store::fromDevice(makeExternalDevice());
    config.entries[0].visible = true;
    config.entries[0].unit = "kHz";
    REQUIRE(store::save("VST3-Surge-XT-1a2b3c4d", config));

    store::refreshLiveDevices("VST3-Surge-XT-1a2b3c4d");

    const auto* track = tm.getTrack(trackId);
    REQUIRE(track != nullptr);

    const magda::DeviceInfo* onPad = magda::chain_walk::findDevice(
        track->chain.fxChainElements, magda::ChainNodePath::trackLevel(trackId),
        magda::chain_walk::Pads::Enter,
        [](const magda::DeviceInfo& device, const magda::ChainNodePath&) {
            return device.uniqueId == "VST3-Surge-XT-1a2b3c4d";
        });
    REQUIRE(onPad != nullptr);

    CHECK(onPad->parameters[0].unit == "kHz");
    CHECK(onPad->visibleParameters == std::vector<int>{0});

    tm.clearAllTracks();
}

TEST_CASE("A partial config update keeps the selections of a device with no config file",
          "[param-config-store][2620]") {
    // devices.setParameterConfig with a prompt and nothing else: it starts
    // from fromDevice, and there is no file to overlay.
    TempDataDir temp;
    auto& tm = magda::TrackManager::getInstance();
    tm.clearAllTracks();

    const auto trackId = tm.createTrack("Track");
    auto device = makeExternalDevice();
    device.visibleParameters = {0, 1};
    device.miniMixerParameters = {2};
    device.aiSoundDesignerParameters = {0};
    const auto deviceId = tm.addDeviceToTrack(trackId, device);
    const auto path = tm.findDevicePath(deviceId);

    magda::DeviceApiLive devices;
    magda::DeviceParameterConfigUpdate update;
    update.aiPrompt = "Warm analog pads";
    REQUIRE(devices.setDeviceParameterConfig(path, update));

    const auto saved = store::load(device.uniqueId);
    REQUIRE(saved.has_value());
    CHECK(saved->aiPrompt == "Warm analog pads");
    CHECK(saved->entries[0].visible);
    CHECK(saved->entries[1].visible);
    CHECK(saved->entries[2].miniMixer);
    CHECK(saved->entries[0].aiAgent);

    // And the config that reaches the live device leaves it as it was.
    const auto* track = tm.getTrack(trackId);
    REQUIRE(track != nullptr);
    REQUIRE(track->chain.fxChainElements.size() == 1);
    const auto& live = magda::getDevice(track->chain.fxChainElements[0]);
    CHECK(live.visibleParameters == std::vector<int>{0, 1});
    CHECK(live.miniMixerParameters == std::vector<int>{2});
    CHECK(live.aiSoundDesignerParameters == std::vector<int>{0});

    tm.clearAllTracks();
}
