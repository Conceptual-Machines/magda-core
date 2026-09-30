#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "LegacyCorpus.hpp"
#include "core/DeviceState.hpp"
#include "magda/daw/audio/plugins/ArpeggiatorPlugin.hpp"
#include "magda/daw/audio/plugins/DeviceCatalogParameters.hpp"
#include "magda/daw/audio/plugins/engine/EngineDeviceFactory.hpp"
#include "magda/daw/audio/plugins/engine/EngineMagdaDevice.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"

// Device state as the native engine reads it (#2556). The model owns the document and
// the device is its projection, so there is no capture: what has to hold is that a read
// is faithful and repeatable, for v2 documents and for the pre-v2 state the legacy
// corpus still carries verbatim.

namespace {

namespace adapter = magda::daw::audio::engine_adapter;
namespace ds = magda::device_state;
namespace corpus = magda::test::legacy_corpus;

struct LegacyDevice {
    juce::String where;
    magda::DeviceInfo device;
};

std::vector<LegacyDevice> collectLegacyDevices() {
    std::vector<LegacyDevice> found;
    for (const auto& entry : corpus::projectFixtures()) {
        if (entry.legacyDeviceStates == 0)
            continue;
        magda::StagedProjectData staged;
        if (!magda::ProjectSerializer::loadAndStage(corpus::projectsDir().getChildFile(entry.file),
                                                    staged))
            continue;
        const auto collect = [&](const magda::TrackInfo& track) {
            corpus::forEachDevice(track, [&](const magda::DeviceInfo& device) {
                if (ds::looksLikeLegacyEngineState(device.pluginState))
                    found.push_back(
                        {juce::String(entry.file) + " / " + track.name + " / " + device.pluginId,
                         device});
            });
        };
        for (const auto& track : staged.tracks)
            collect(track);
        if (staged.masterTrack != nullptr)
            collect(*staged.masterTrack);
    }
    return found;
}

/// What the running device reports as its own state.
juce::ValueTree reported(magda::daw::audio::MagdaDevice& device) {
    juce::ValueTree state("STATE");
    device.flushState(state);
    return state;
}

}  // namespace

TEST_CASE("Every pre-v2 device state in the corpus restores into a native device",
          "[corpus][native][2556]") {
    const auto legacy = collectLegacyDevices();
    REQUIRE_FALSE(legacy.empty());

    int restored = 0;
    for (const auto& [where, model] : legacy) {
        INFO(where);
        const auto saved = magda::daw::audio::deviceStateTree(model.pluginState);
        REQUIRE(saved.isValid());

        auto engineDevice = adapter::createEngineDevice(model);
        auto* hosted = dynamic_cast<adapter::EngineMagdaDevice*>(engineDevice.get());
        if (hosted == nullptr) {
            // External plugins and devices this build does not host natively.
            UNSCOPED_INFO("not a native internal device here, unchecked: " << where);
            continue;
        }

        // What the device reports matches what the project saved, wherever both speak.
        const auto first = reported(hosted->device());
        for (int i = 0; i < first.getNumProperties(); ++i) {
            const auto name = first.getPropertyName(i);
            if (saved.hasProperty(name)) {
                INFO("property " << name.toString());
                CHECK(first[name].toString() == saved[name].toString());
            }
        }

        // Loading what it reports gives the same device again, so reopening is stable.
        auto again = magda::daw::audio::createDetachedDevice(model.pluginId);
        REQUIRE(again != nullptr);
        again->restoreState(first);
        CHECK(reported(*again).isEquivalentTo(first));
        ++restored;
    }
    CHECK(restored > 0);
}

TEST_CASE("A v2 document's authored settings reach a native device and read back unchanged",
          "[device-state][native][2556]") {
    ds::Doc doc;
    doc.deviceType = magda::daw::audio::ArpeggiatorPlugin::xmlTypeName;
    doc.root.props.set("arpQuantizeSub", 8);
    doc.root.props.set("arpHardAngle", true);

    magda::DeviceInfo model;
    model.pluginId = magda::daw::audio::ArpeggiatorPlugin::xmlTypeName;
    model.pluginState = ds::encode(doc);

    auto engineDevice = adapter::createEngineDevice(model);
    auto* hosted = dynamic_cast<adapter::EngineMagdaDevice*>(engineDevice.get());
    REQUIRE(hosted != nullptr);
    auto* arp = dynamic_cast<magda::daw::audio::ArpeggiatorPlugin*>(&hosted->device());
    REQUIRE(arp != nullptr);
    CHECK(arp->quantizeSub.load() == 8);
    CHECK(arp->hardAngle.load());

    const auto first = reported(*arp);
    CHECK(static_cast<int>(first["arpQuantizeSub"]) == 8);
    CHECK(static_cast<bool>(first["arpHardAngle"]));

    auto again = magda::daw::audio::createDetachedDevice(model.pluginId);
    REQUIRE(again != nullptr);
    again->restoreState(first);
    CHECK(reported(*again).isEquivalentTo(first));
}
