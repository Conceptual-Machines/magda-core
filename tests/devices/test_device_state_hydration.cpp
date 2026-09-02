#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../../magda/daw/audio/plugins/ArpeggiatorPlugin.hpp"
#include "../../magda/daw/audio/plugins/DevicePluginHandle.hpp"
#include "../../magda/daw/audio/plugins/DeviceStateHydration.hpp"
#include "../../magda/daw/audio/plugins/InternalPluginRegistry.hpp"
#include "../../magda/daw/audio/plugins/MagdaDevice.hpp"
#include "../../magda/daw/audio/plugins/SidechainPlugin.hpp"
#include "../../magda/daw/audio/plugins/compiled/CompiledPluginRegistry.hpp"
#include "../../magda/daw/core/DeviceState.hpp"
#include "../../magda/daw/core/ParameterUtils.hpp"

using namespace magda;
namespace ds = magda::device_state;
namespace hydration = magda::daw::audio::device_state_hydration;

// ============================================================================
// #2317 — one-time hydration of DeviceInfo::parameters from pre-#2317 device
// state documents. The model array is the sole parameter authority; these are
// the chronology rules that read every older document era into it exactly once.
// ============================================================================

namespace {

DeviceInfo internalDevice(const juce::String& pluginId) {
    DeviceInfo device;
    device.id = 1;
    device.format = PluginFormat::Internal;
    device.pluginId = pluginId;
    return device;
}

/// The display metadata the hydration itself consults, used as the oracle for
/// normalised->display expectations.
ParameterInfo slotMetadata(const juce::String& pluginId, int slot) {
    const auto* spec = daw::audio::findInternalPluginSpec(pluginId);
    REQUIRE(spec != nullptr);
    REQUIRE(spec->createDevice != nullptr);
    juce::ValueTree state{juce::Identifier("PLUGIN")};
    state.setProperty(juce::Identifier("type"), pluginId, nullptr);
    auto device = spec->createDevice({.sessionKey = {}, .state = state, .isNewPlugin = true});
    REQUIRE(device != nullptr);
    REQUIRE(slot < device->parameterCount());
    return device->parameterInfo(slot);
}

const ParameterInfo* paramAt(const DeviceInfo& device, int index) {
    return device.findParameterByIndex(index);
}

}  // namespace

TEST_CASE("Hydration fills an empty model array from a marked display document",
          "[device-state-hydration]") {
    auto device = internalDevice(daw::audio::ArpeggiatorPlugin::xmlTypeName);

    ds::Doc doc;
    doc.deviceType = device.pluginId;
    doc.paramsAreDisplayDomain = true;
    doc.params.push_back({daw::audio::ArpeggiatorPlugin::kFixedVel, "fixedvel", 100.0f});
    doc.params.push_back({daw::audio::ArpeggiatorPlugin::kGate, "gate", 0.8f});
    device.pluginState = ds::encode(doc);

    REQUIRE(hydration::hydrateParametersFromDeviceState(device));

    const auto* fixedVel = paramAt(device, daw::audio::ArpeggiatorPlugin::kFixedVel);
    REQUIRE(fixedVel != nullptr);
    CHECK(fixedVel->currentValue == Catch::Approx(100.0f));
    // Real metadata, not a placeholder: the plan's value layer converts through
    // these ranges.
    CHECK(fixedVel->maxValue > 100.0f);
    CHECK(fixedVel->name.isNotEmpty());

    const auto* gate = paramAt(device, daw::audio::ArpeggiatorPlugin::kGate);
    REQUIRE(gate != nullptr);
    CHECK(gate->currentValue == Catch::Approx(0.8f));
}

TEST_CASE("Hydration never overwrites a parameter the model already carries",
          "[device-state-hydration]") {
    auto device = internalDevice(daw::audio::ArpeggiatorPlugin::xmlTypeName);

    ParameterInfo existing;
    existing.paramIndex = daw::audio::ArpeggiatorPlugin::kFixedVel;
    existing.name = "Fixed Vel";
    existing.currentValue = 64.0f;
    device.parameters.push_back(existing);

    ds::Doc doc;
    doc.deviceType = device.pluginId;
    doc.paramsAreDisplayDomain = true;
    doc.params.push_back({daw::audio::ArpeggiatorPlugin::kFixedVel, "fixedvel", 100.0f});
    device.pluginState = ds::encode(doc);

    REQUIRE_FALSE(hydration::hydrateParametersFromDeviceState(device));
    CHECK(paramAt(device, daw::audio::ArpeggiatorPlugin::kFixedVel)->currentValue ==
          Catch::Approx(64.0f));
}

TEST_CASE("An unmarked document for a device ported with the marker reads as display",
          "[device-state-hydration]") {
    // The #2312 six were never behind the wrapper before the marker existed, so
    // an unmarked document can only be a capture from the retired display-ranged
    // plugin - even when every value happens to sit inside [0, 1].
    auto device = internalDevice(daw::audio::ArpeggiatorPlugin::xmlTypeName);

    ds::Doc doc;
    doc.deviceType = device.pluginId;
    doc.params.push_back({daw::audio::ArpeggiatorPlugin::kGate, "gate", 0.8f});
    device.pluginState = ds::encode(doc);
    REQUIRE_FALSE(ds::decode(device.pluginState)->paramsAreDisplayDomain);

    REQUIRE(hydration::hydrateParametersFromDeviceState(device));
    CHECK(paramAt(device, daw::audio::ArpeggiatorPlugin::kGate)->currentValue ==
          Catch::Approx(0.8f));
}

TEST_CASE("Unmarked two-era sidechain documents resolve released-state-first",
          "[device-state-hydration]") {
    // The sidechain was wrapped before the marker existed, so its unmarked
    // documents have two eras. Any value outside [0, 1] proves the display era.
    SECTION("a 0.19 display document is identified by its out-of-range value") {
        auto device = internalDevice(daw::audio::SidechainPlugin::xmlTypeName);

        ds::Doc doc;
        doc.deviceType = device.pluginId;
        doc.params.push_back({daw::audio::SidechainPlugin::kGainParamIndex, "gain", 0.8f});
        doc.params.push_back({daw::audio::SidechainPlugin::kReleaseParamIndex, "release", 15.0f});
        device.pluginState = ds::encode(doc);

        REQUIRE(hydration::hydrateParametersFromDeviceState(device));
        CHECK(paramAt(device, daw::audio::SidechainPlugin::kReleaseParamIndex)->currentValue ==
              Catch::Approx(15.0f));
        CHECK(paramAt(device, daw::audio::SidechainPlugin::kGainParamIndex)->currentValue ==
              Catch::Approx(0.8f));
    }

    SECTION("the all-unit-interval residue reads as normalised") {
        auto device = internalDevice(daw::audio::SidechainPlugin::xmlTypeName);

        ds::Doc doc;
        doc.deviceType = device.pluginId;
        doc.params.push_back({daw::audio::SidechainPlugin::kAttackParamIndex, "attack", 0.5f});
        device.pluginState = ds::encode(doc);

        REQUIRE(hydration::hydrateParametersFromDeviceState(device));

        const auto info =
            slotMetadata(device.pluginId, daw::audio::SidechainPlugin::kAttackParamIndex);
        const auto expected = ParameterUtils::normalizedToReal(0.5f, info);
        CHECK(paramAt(device, daw::audio::SidechainPlugin::kAttackParamIndex)->currentValue ==
              Catch::Approx(expected).margin(1.0e-3));
    }

    SECTION("pre-0.20 provenance overrides the residue rule") {
        auto device = internalDevice(daw::audio::SidechainPlugin::xmlTypeName);

        ds::Doc doc;
        doc.deviceType = device.pluginId;
        doc.params.push_back({daw::audio::SidechainPlugin::kAttackParamIndex, "attack", 0.5f});
        device.pluginState = ds::encode(doc);

        REQUIRE(hydration::hydrateParametersFromDeviceState(
            device, hydration::provenanceFromMagdaVersion("0.19.2")));
        CHECK(paramAt(device, daw::audio::SidechainPlugin::kAttackParamIndex)->currentValue ==
              Catch::Approx(0.5f));
    }
}

TEST_CASE("A compiled-pack document is always normalised", "[device-state-hydration]") {
    // The compiled Faust pack's parameters were normalised slots in every era,
    // documents included, and its documents were never marked.
    const juce::String compiledId = "magda_utility";
    const auto* spec = daw::audio::compiled::findCompiledPluginSpec(compiledId);
    if (spec == nullptr || spec->createDevice == nullptr)
        SKIP("compiled pack not linked into this test build");

    auto device = internalDevice(compiledId);
    ds::Doc doc;
    doc.deviceType = compiledId;
    doc.params.push_back({0, "", 0.5f});
    device.pluginState = ds::encode(doc);

    REQUIRE(hydration::hydrateParametersFromDeviceState(device));

    juce::ValueTree state{juce::Identifier("PLUGIN")};
    state.setProperty(juce::Identifier("type"), compiledId, nullptr);
    auto sdk = spec->createDevice({.sessionKey = {}, .state = state, .isNewPlugin = true});
    REQUIRE(sdk != nullptr);
    const auto expected = ParameterUtils::normalizedToReal(0.5f, sdk->parameterInfo(0));
    CHECK(paramAt(device, 0)->currentValue == Catch::Approx(expected).margin(1.0e-3));
}

TEST_CASE("Hydration leaves what it cannot read alone", "[device-state-hydration]") {
    SECTION("the canonical params-less document is a no-op") {
        auto device = internalDevice(daw::audio::ArpeggiatorPlugin::xmlTypeName);
        ds::Doc doc;
        doc.deviceType = device.pluginId;
        doc.root.props.set(daw::audio::ArpeggiatorPlugin::SettingIDs::quantizeSub, 8);
        device.pluginState = ds::encode(doc);

        REQUIRE_FALSE(hydration::hydrateParametersFromDeviceState(device));
        CHECK(device.parameters.empty());
    }

    SECTION("a future-schema document is not decoded") {
        auto device = internalDevice(daw::audio::ArpeggiatorPlugin::xmlTypeName);
        device.pluginState = juce::String("{\"schema\": 99, \"device\": \"arpeggiator\", ") +
                             "\"params\": [{\"i\": 0, \"v\": 0.5}]}";

        REQUIRE_FALSE(hydration::hydrateParametersFromDeviceState(device));
        CHECK(device.parameters.empty());
    }

    SECTION("pre-v2 engine XML is not a document") {
        auto device = internalDevice(daw::audio::ArpeggiatorPlugin::xmlTypeName);
        device.pluginState = "<PLUGIN type=\"arpeggiator\" gate=\"0.4\"/>";

        REQUIRE_FALSE(hydration::hydrateParametersFromDeviceState(device));
        CHECK(device.parameters.empty());
    }

    SECTION("an external device is never touched") {
        DeviceInfo device;
        device.format = PluginFormat::VST3;
        device.pluginState = "somebase64chunk";

        REQUIRE_FALSE(hydration::hydrateParametersFromDeviceState(device));
    }
}

TEST_CASE("Provenance parses the container's magdaVersion", "[device-state-hydration]") {
    CHECK(hydration::provenanceFromMagdaVersion("0.19.2").savedBeforeWrapperCutover);
    CHECK(hydration::provenanceFromMagdaVersion("0.7.0").savedBeforeWrapperCutover);
    CHECK_FALSE(hydration::provenanceFromMagdaVersion("0.20.0").savedBeforeWrapperCutover);
    CHECK_FALSE(hydration::provenanceFromMagdaVersion("0.21.3").savedBeforeWrapperCutover);
    CHECK_FALSE(hydration::provenanceFromMagdaVersion("1.0.0").savedBeforeWrapperCutover);
    // No version at all: nothing that old postdates the cutover.
    CHECK(hydration::provenanceFromMagdaVersion("").savedBeforeWrapperCutover);
    CHECK(hydration::provenanceFromMagdaVersion("unknown").savedBeforeWrapperCutover);
}
