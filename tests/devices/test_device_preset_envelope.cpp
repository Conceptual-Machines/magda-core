#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <magda/sdk/preset/Preset.hpp>

#include "magda/daw/audio/plugins/DeviceCatalogParameters.hpp"
#include "magda/daw/audio/plugins/DeviceStateDocument.hpp"
#include "magda/daw/audio/plugins/compiled/MagdaPolySynthCompiledPlugin.hpp"
#include "magda/daw/core/AppPaths.hpp"
#include "magda/daw/core/Config.hpp"
#include "magda/daw/core/DevicePresetEnvelope.hpp"
#include "magda/daw/core/DeviceState.hpp"
#include "magda/daw/core/PresetManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"

using namespace magda;

// ============================================================================
// Device presets in the SDK's portable envelope (#2939)
// ============================================================================

namespace {

const char* kPolySynth = magda::daw::audio::compiled::MagdaPolySynthCompiledPlugin::xmlTypeName;

/// Presets in a scratch folder, with a sibling folder for the files they point at.
struct ScratchPresets {
    ScratchPresets()
        : root(juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getNonexistentChildFile("magda-preset-envelope", {})),
          previous(Config::getInstance().getPresetsDir()) {
        REQUIRE(root.createDirectory());
        Config::getInstance().setPresetsDir(
            root.getChildFile("presets").getFullPathName().toStdString());
        paths::resolve();
    }

    ~ScratchPresets() {
        Config::getInstance().setPresetsDir(previous);
        paths::resolve();
        root.deleteRecursively();
    }

    juce::File file(const juce::String& folder, const juce::String& name) const {
        return PresetManager::getInstance().getDevicePluginDirectory(folder).getChildFile(name +
                                                                                          ".mps");
    }

    juce::File root;
    std::string previous;
};

DeviceInfo internalDevice(const juce::String& pluginId) {
    DeviceInfo device;
    device.id = 7;
    device.name = pluginId;
    device.format = PluginFormat::Internal;
    device.pluginId = pluginId;
    daw::audio::applyDeviceDeclaration(device);
    return device;
}

ControlTarget parameterTarget(int index) {
    ControlTarget target;
    target.kind = ControlTarget::Kind::PluginParam;
    target.devicePath = ChainNodePath::topLevelDevice(1, 7);
    target.paramIndex = index;
    return target;
}

/// The file as the builds before the envelope wrote it.
void writeLegacyPreset(const juce::File& file, const DeviceInfo& device, const juce::String& id) {
    auto* envelope = new juce::DynamicObject();
    envelope->setProperty("magdaVersion", "0.19.0");
    envelope->setProperty("kind", "device");
    envelope->setProperty("id", id);
    envelope->setProperty("payload", ProjectSerializer::serializeDeviceInfo(device));
    file.getParentDirectory().createDirectory();
    REQUIRE(file.replaceWithText(juce::JSON::toString(juce::var(envelope), false)));
}

DeviceInfo load(const juce::String& folder, const juce::String& name) {
    DeviceInfo device;
    INFO(PresetManager::getInstance().getLastError());
    REQUIRE(PresetManager::getInstance().loadDevicePreset(folder, name, device));
    return device;
}

/// A device as JSON, with its saved state read as the document it holds.
juce::String describe(DeviceInfo device) {
    if (device.format == PluginFormat::Internal && device.hasPluginState())
        if (const auto state = daw::audio::normaliseDeviceState(device.pluginState))
            device.pluginState = juce::String::fromUTF8(
                sdk::encodeDocument(state->document).value_or(std::string()).c_str());
    return juce::JSON::toString(ProjectSerializer::serializeDeviceInfo(device), false);
}

juce::String idOf(const juce::File& file) {
    return juce::JSON::parse(file.loadFileAsString()).getProperty("id", {}).toString();
}

bool isEnvelope(const juce::File& file) {
    return sdk::readPreset(file.loadFileAsString().toStdString()).ok();
}

void requireSameFromBothFormats(const ScratchPresets& scratch, const DeviceInfo& device) {
    writeLegacyPreset(scratch.file(device.name, "Old"), device, "legacy-id");
    REQUIRE(PresetManager::getInstance().saveDevicePreset(device, "New"));
    REQUIRE(isEnvelope(scratch.file(device.name, "New")));

    const auto fromOld = load(device.name, "Old");
    const auto fromNew = load(device.name, "New");
    CHECK(describe(fromNew) == describe(fromOld));
}

}  // namespace

TEST_CASE("An internal device with macros and mods loads the same from both formats",
          "[preset-envelope]") {
    ScratchPresets scratch;
    auto device = internalDevice(kPolySynth);
    REQUIRE(device.parameters.size() > 3);
    device.parameters[1].currentValue = device.parameters[1].maxValue * 0.5f;
    device.parameters[2].currentValue = device.parameters[2].minValue;
    device.visibleParameters = {1, 2};
    device.gainDb = -3.0f;
    device.gainValue = 0.7079f;

    device.macros[0].name = "Bright";
    device.macros[0].value = 0.25f;
    device.macros[0].links.push_back({parameterTarget(2), 0.5f, true});
    ModInfo lfo(0);
    lfo.name = "Wobble";
    lfo.rate = 3.5f;
    lfo.links.push_back({parameterTarget(3), 0.75f, false, true});
    device.mods.push_back(lfo);

    requireSameFromBothFormats(scratch, device);
}

TEST_CASE("The envelope keys parameters and links by stable id", "[preset-envelope]") {
    ScratchPresets scratch;
    auto device = internalDevice(kPolySynth);
    device.parameters[1].currentValue = device.parameters[1].maxValue * 0.5f;
    device.macros[0].links.push_back({parameterTarget(1), 1.0f, false});
    REQUIRE(PresetManager::getInstance().saveDevicePreset(device, "Keyed"));

    const auto text = scratch.file(device.name, "Keyed").loadFileAsString();
    const auto read = sdk::readPreset(text.toStdString());
    REQUIRE(read.ok());
    CHECK(read.preset->valueDomain == sdk::PresetValueDomain::Display);
    CHECK(read.preset->writer.starts_with("MAGDA "));
    CHECK(read.preset->name == "Keyed");
    CHECK(read.preset->deviceType == juce::String(kPolySynth).toStdString());
    CHECK(read.preset->parameters.size() == device.parameters.size());
    CHECK(text.contains("\"paramIndex\"") == false);

    const auto first = device.parameters[1].stableId.isNotEmpty()
                           ? device.parameters[1].stableId
                           : juce::String(kPolySynth) + "_param_1";
    const auto found =
        std::ranges::find(read.preset->parameters, first.toStdString(), &sdk::PresetParameter::id);
    REQUIRE(found != read.preset->parameters.end());
    CHECK(found->value == Catch::Approx(device.parameters[1].currentValue));
    CHECK(text.contains("\"stableId\": \"" + first + "\""));
}

TEST_CASE("The Sidechain device keeps its curve", "[preset-envelope]") {
    ScratchPresets scratch;
    DeviceInfo device;
    device.id = 7;
    device.name = "Sidechain";
    device.format = PluginFormat::Internal;
    device.pluginId = "sidechain";
    TrackManager::seedSidechainModIfMissing(device, ChainNodePath::topLevelDevice(1, 7));
    daw::audio::applyDeviceDeclaration(device);
    REQUIRE(device.mods.size() == 1);
    REQUIRE_FALSE(device.mods[0].curvePoints.empty());
    device.sidechainPort = monoAudioSidechain;

    requireSameFromBothFormats(scratch, device);

    const auto loaded = load("Sidechain", "New");
    REQUIRE(loaded.mods.size() == 1);
    CHECK(loaded.mods[0].curvePoints.size() == device.mods[0].curvePoints.size());
    CHECK(loaded.mods[0].invertOutput);
    CHECK(loaded.sidechainPort.declared());
}

TEST_CASE("A Sampler's file is an asset relative to the preset", "[preset-envelope]") {
    ScratchPresets scratch;
    const auto sample = scratch.root.getChildFile("samples/kick one.wav");
    REQUIRE(sample.getParentDirectory().createDirectory());
    REQUIRE(sample.replaceWithText("not really a wav"));

    auto device = internalDevice("magdasampler");
    device_state::Doc doc;
    doc.deviceType = "magdasampler";
    doc.root.props.set("source", sample.getFullPathName());
    doc.root.props.set("rootNote", 60);
    device.pluginState = device_state::encode(doc);

    requireSameFromBothFormats(scratch, device);

    const auto file = scratch.file(device.name, "New");
    const auto read = sdk::readPreset(file.loadFileAsString().toStdString());
    REQUIRE(read.ok());
    REQUIRE(read.preset->assets.size() == 1);
    CHECK(read.preset->assets[0].key == "sample");
    CHECK(read.preset->assets[0].path == "../../../samples/kick one.wav");
    CHECK(read.preset->assets[0].sha256 == juce::SHA256(sample).toHexString().toStdString());

    const auto& state = std::get<sdk::PresetMagdaDevice>(read.preset->device).state;
    CHECK(state.root.getString("source") == "sample");
    CHECK_FALSE(file.loadFileAsString().contains(scratch.root.getFullPathName()));

    const auto loaded = load(device.name, "New");
    const auto loadedState = device_state::decode(loaded.pluginState);
    REQUIRE(loadedState.has_value());
    CHECK(loadedState->root.props["source"].toString() == sample.getFullPathName());
    CHECK(loadedState->root.props["rootNote"] == juce::var(60));
}

TEST_CASE("A missing asset still loads and is reported", "[preset-envelope]") {
    ScratchPresets scratch;
    const auto sample = scratch.root.getChildFile("samples/gone.wav");
    REQUIRE(sample.getParentDirectory().createDirectory());
    REQUIRE(sample.replaceWithText("x"));

    auto device = internalDevice("magdasampler");
    device_state::Doc doc;
    doc.deviceType = "magdasampler";
    doc.root.props.set("source", sample.getFullPathName());
    device.pluginState = device_state::encode(doc);
    REQUIRE(PresetManager::getInstance().saveDevicePreset(device, "Gone"));
    const auto file = scratch.file(device.name, "Gone");

    REQUIRE(sample.deleteFile());
    DeviceInfo loaded;
    const auto result = device_preset::readEnvelope(file.loadFileAsString(), file, loaded);
    REQUIRE(result.status == device_preset::ReadStatus::Loaded);
    REQUIRE(result.warnings.size() == 1);
    CHECK(result.warnings[0].contains("gone.wav"));
    CHECK(device_state::decode(loaded.pluginState)->root.props["source"].toString() ==
          sample.getFullPathName());
}

TEST_CASE("Renaming a preset into another folder keeps its asset reachable", "[preset-envelope]") {
    ScratchPresets scratch;
    const auto sample = scratch.root.getChildFile("samples/kick.wav");
    REQUIRE(sample.getParentDirectory().createDirectory());
    REQUIRE(sample.replaceWithText("kick"));

    auto device = internalDevice("magdasampler");
    device_state::Doc doc;
    doc.deviceType = "magdasampler";
    doc.root.props.set("source", sample.getFullPathName());
    device.pluginState = device_state::encode(doc);
    REQUIRE(PresetManager::getInstance().saveDevicePreset(device, "Kick"));
    const auto id = idOf(scratch.file(device.name, "Kick"));

    REQUIRE(PresetManager::getInstance().renameDevicePreset(device.name, "Kick", "Drums/Kick"));
    const auto moved = scratch.file(device.name, "Drums/Kick");
    CHECK(idOf(moved) == id);

    const auto loaded = load(device.name, "Drums/Kick");
    CHECK(device_state::decode(loaded.pluginState)->root.props["source"].toString() ==
          sample.getFullPathName());
}

TEST_CASE("An external plugin's chunk loads the same from both formats", "[preset-envelope]") {
    ScratchPresets scratch;
    DeviceInfo device;
    device.id = 7;
    device.name = "Some Synth";
    device.pluginId = "Some Synth";
    device.manufacturer = "Acme";
    device.format = PluginFormat::VST3;
    device.isInstrument = true;
    device.deviceType = DeviceType::Instrument;
    device.uniqueId = "VST3-Some-1234";
    device.fileOrIdentifier = "/Library/Audio/Plug-Ins/VST3/Some.vst3";
    for (int i = 0; i < 3; ++i) {
        ParameterInfo parameter;
        parameter.paramIndex = i;
        parameter.name = "Param " + juce::String(i);
        parameter.valueConvention = ParameterValueConvention::Normalized;
        parameter.defaultValue = 0.5f;
        parameter.currentValue = 0.1f * static_cast<float>(i + 1);
        device.parameters.push_back(parameter);
    }
    const juce::MemoryBlock chunk("\x01\x02plugin state\xff", 14);
    device.pluginState = chunk.toBase64Encoding();
    device.macros[0].links.push_back({parameterTarget(1), 0.4f, false});

    requireSameFromBothFormats(scratch, device);

    const auto read =
        sdk::readPreset(scratch.file(device.name, "New").loadFileAsString().toStdString());
    REQUIRE(read.ok());
    CHECK(read.preset->valueDomain == sdk::PresetValueDomain::Normalized);
    const auto& plugin = std::get<sdk::PresetPluginDevice>(read.preset->device);
    CHECK(plugin.format == "VST3");
    CHECK(plugin.uniqueId == "VST3-Some-1234");
    CHECK(plugin.chunk.size() == chunk.getSize());
    CHECK(read.preset->parameters[1].id == "Some Synth_param_1");
}

TEST_CASE("A plugin's VST3 class id and preset round-trip", "[preset-envelope]") {
    ScratchPresets scratch;
    DeviceInfo device;
    device.name = device.pluginId = "Some Synth";
    device.format = PluginFormat::VST3;
    device.uniqueId = "VST3-Some-1234";
    device.vst3ClassId = "ABCDEF0123456789ABCDEF0123456789";
    const juce::MemoryBlock preset("VST3 preset bytes", 17);
    device.vst3Preset = juce::Base64::toBase64(preset.getData(), preset.getSize());

    REQUIRE(PresetManager::getInstance().saveDevicePreset(device, "Classy"));
    const auto loaded = load(device.name, "Classy");
    CHECK(loaded.vst3ClassId == device.vst3ClassId);
    CHECK(loaded.vst3Preset == device.vst3Preset);
}

TEST_CASE("Unknown parameters are reported and missing ones keep their defaults",
          "[preset-envelope]") {
    ScratchPresets scratch;
    auto device = internalDevice(kPolySynth);
    device.parameters[1].currentValue = device.parameters[1].maxValue;
    REQUIRE(PresetManager::getInstance().saveDevicePreset(device, "Edited"));
    const auto file = scratch.file(device.name, "Edited");

    auto read = sdk::readPreset(file.loadFileAsString().toStdString());
    REQUIRE(read.ok());
    read.preset->parameters.erase(read.preset->parameters.begin() + 1);
    read.preset->parameters.push_back({"no_such_parameter", 0.5});
    std::string error;
    const auto edited = sdk::writePreset(*read.preset, error);
    REQUIRE(edited.has_value());

    DeviceInfo loaded;
    const auto result =
        device_preset::readEnvelope(juce::String::fromUTF8(edited->c_str()), file, loaded);
    REQUIRE(result.status == device_preset::ReadStatus::Loaded);
    REQUIRE(result.warnings.size() == 1);
    CHECK(result.warnings[0].contains("no_such_parameter"));

    const auto* restored = loaded.findParameterByIndex(device.parameters[1].paramIndex);
    REQUIRE(restored != nullptr);
    CHECK(restored->currentValue == Catch::Approx(restored->defaultValue));
}

TEST_CASE("A preset from another host loads with its values on the declared parameters",
          "[preset-envelope]") {
    ScratchPresets scratch;
    const auto declared = internalDevice(kPolySynth);
    REQUIRE(declared.parameters.size() > 2);
    const auto& target = declared.parameters[2];
    const auto id = target.stableId.isNotEmpty()
                        ? target.stableId
                        : juce::String(kPolySynth) + "_param_" + juce::String(target.paramIndex);
    const float value = target.minValue + 0.25f * (target.maxValue - target.minValue);

    sdk::Preset preset;
    preset.id = "foreign-1";
    preset.writer = "Other Host 2";
    preset.deviceType = kPolySynth;
    preset.name = "Foreign";
    preset.created = "2026-01-02T03:04:05Z";
    preset.parameters = {{id.toStdString(), static_cast<double>(value)}};
    sdk::StateDocument state;
    state.deviceType = kPolySynth;
    preset.device = sdk::PresetMagdaDevice{state};
    std::string error;
    const auto text = sdk::writePreset(preset, error);
    REQUIRE(text.has_value());

    const auto file = scratch.file("Poly", "Foreign");
    REQUIRE(file.getParentDirectory().createDirectory());
    REQUIRE(file.replaceWithText(juce::String::fromUTF8(text->c_str())));

    const auto loaded = load("Poly", "Foreign");
    CHECK(loaded.pluginId == juce::String(kPolySynth));
    CHECK(loaded.format == PluginFormat::Internal);
    const auto* parameter = loaded.findParameterByIndex(target.paramIndex);
    REQUIRE(parameter != nullptr);
    CHECK(parameter->currentValue == Catch::Approx(value));
}

TEST_CASE("A legacy preset keeps its id through load, overwrite and rename", "[preset-envelope]") {
    ScratchPresets scratch;
    const auto device = internalDevice(kPolySynth);
    writeLegacyPreset(scratch.file(device.name, "Old"), device, "legacy-id");

    auto& manager = PresetManager::getInstance();
    const auto listed = manager.getDevicePresetMetadata(device.name);
    REQUIRE(listed.size() == 1);
    CHECK(listed[0].id == "device-preset:legacy-id");
    (void)load(device.name, "Old");

    REQUIRE(manager.saveDevicePreset(device, "Old"));
    CHECK(isEnvelope(scratch.file(device.name, "Old")));
    CHECK(manager.getDevicePresetMetadata(device.name)[0].id == "device-preset:legacy-id");

    REQUIRE(manager.renameDevicePreset(device.name, "Old", "Renamed"));
    CHECK(manager.getDevicePresetMetadata(device.name)[0].id == "device-preset:legacy-id");

    DeviceInfo viaId;
    REQUIRE(manager.loadDevicePresetById(device.name, "device-preset:legacy-id", viaId));
    CHECK(viaId.pluginId == device.pluginId);
}

TEST_CASE("A new preset gets a UUID and keeps author and creation time on overwrite",
          "[preset-envelope]") {
    ScratchPresets scratch;
    const auto device = internalDevice(kPolySynth);
    auto& manager = PresetManager::getInstance();
    REQUIRE(manager.saveDevicePreset(device, "Fresh"));
    const auto file = scratch.file(device.name, "Fresh");

    auto read = sdk::readPreset(file.loadFileAsString().toStdString());
    REQUIRE(read.ok());
    CHECK(juce::Uuid(juce::String(read.preset->id)) != juce::Uuid::null());
    read.preset->author = "Ada";
    read.preset->tags = {"pad"};
    read.preset->created = "2020-05-06T07:08:09Z";
    std::string error;
    REQUIRE(file.replaceWithText(
        juce::String::fromUTF8(sdk::writePreset(*read.preset, error)->c_str())));

    REQUIRE(manager.saveDevicePreset(device, "Fresh"));
    const auto again = sdk::readPreset(file.loadFileAsString().toStdString());
    REQUIRE(again.ok());
    CHECK(again.preset->id == read.preset->id);
    CHECK(again.preset->author == "Ada");
    CHECK(again.preset->tags == std::vector<std::string>{"pad"});
    CHECK(again.preset->created == "2020-05-06T07:08:09Z");
}

TEST_CASE("A device the envelope cannot hold exactly keeps the legacy payload",
          "[preset-envelope]") {
    ScratchPresets scratch;
    auto device = internalDevice(kPolySynth);
    device_state::Doc doc;
    doc.deviceType = kPolySynth;
    juce::Array<juce::var> list;
    list.add(1);
    doc.root.props.set("list", juce::var(list));
    device.pluginState = device_state::encode(doc);

    REQUIRE(PresetManager::getInstance().saveDevicePreset(device, "Odd"));
    const auto file = scratch.file(device.name, "Odd");
    CHECK_FALSE(isEnvelope(file));
    CHECK(juce::JSON::parse(file.loadFileAsString()).getProperty("kind", {}) ==
          juce::var("device"));
    (void)load(device.name, "Odd");
}

TEST_CASE("A preset from a newer version is refused and left alone", "[preset-envelope]") {
    ScratchPresets scratch;
    const auto device = internalDevice(kPolySynth);
    REQUIRE(PresetManager::getInstance().saveDevicePreset(device, "Later"));
    const auto file = scratch.file(device.name, "Later");
    const auto future = file.loadFileAsString().replace("\"version\": 1,", "\"version\": 2,");
    REQUIRE(file.replaceWithText(future));

    DeviceInfo loaded;
    CHECK_FALSE(PresetManager::getInstance().loadDevicePreset(device.name, "Later", loaded));
    CHECK(PresetManager::getInstance().getLastError().contains("newer"));
    CHECK(file.loadFileAsString() == future);
}

TEST_CASE("Chain presets stay in the legacy format", "[preset-envelope]") {
    ScratchPresets scratch;
    std::vector<ChainElement> elements;
    elements.emplace_back(internalDevice(kPolySynth));
    auto& manager = PresetManager::getInstance();
    REQUIRE(manager.saveChainPreset(elements, "Chain"));

    const auto file = manager.getChainsDirectory().getChildFile("Chain.mps");
    const auto root = juce::JSON::parse(file.loadFileAsString());
    CHECK(root.getProperty("kind", {}) == juce::var("chain"));
    CHECK_FALSE(root.hasProperty("format"));

    std::vector<ChainElement> loaded;
    REQUIRE(manager.loadChainPreset("Chain", loaded));
    CHECK(loaded.size() == 1);
}
