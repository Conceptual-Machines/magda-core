#include <catch2/catch_test_macros.hpp>

#include "magda/daw/audio/DeviceParameterDisplayTextProvider.hpp"
#include "magda/daw/core/ChainNodePath.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/DeviceInfo.hpp"
#include "magda/daw/core/ParameterInfo.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

using namespace magda;

/**
 * @file
 * @brief Which device a parameter's text provider reaches (#2600).
 *
 * A hosted plugin's parameters are normalised, so the plugin's own string is
 * the only thing there is to display -- and the provider that fetches it has to
 * name the right plugin. A DeviceId is section-local (#1899), so the section is
 * half of the address, and a lookup that assumed the main FX tree would answer
 * with whichever unrelated device holds the same number there.
 */

namespace {

void resetState() {
    ClipManager::getInstance().clearAllClips();
    TrackManager::getInstance().clearAllTracks();
    SelectionManager::getInstance().clearSelection();
    UndoManager::getInstance().clearHistory();
}

DeviceInfo deviceNamed(DeviceId id, const juce::String& name) {
    DeviceInfo device;
    device.id = id;
    device.name = name;
    device.format = PluginFormat::VST3;

    ParameterInfo parameter;
    parameter.paramIndex = 2;
    parameter.name = "Gain";
    device.parameters.push_back(parameter);

    return device;
}

}  // namespace

TEST_CASE("A device id resolves within the section it was asked for", "[devices][2600]") {
    resetState();

    auto& tm = TrackManager::getInstance();
    const auto trackId = tm.createTrack("Track");

    // The same number in two sections, which is the normal shape rather than a
    // contrived one: the two counters are independent.
    constexpr DeviceId kShared = 1;

    auto& track = *tm.getTrack(trackId);
    track.chain.fxChainElements.push_back(
        makeDeviceElement(deviceNamed(kShared, "In the FX tree")));
    track.chain.postFxChainElements.push_back(
        PostFxChainElement{deviceNamed(kShared, "Post fader")});

    const auto fx = tm.findDevicePath(kShared, ChainSegment::Fx);
    const auto postFx = tm.findDevicePath(kShared, ChainSegment::PostFx);

    REQUIRE(fx.isValid());
    REQUIRE(postFx.isValid());
    CHECK_FALSE(fx == postFx);

    // Each names its own device, which is the whole point: resolving the
    // post-fader id through the FX tree would have found the other one.
    const auto* inFx = tm.getDeviceInChainByPath(fx);
    const auto* inPostFx = tm.getDeviceInChainByPath(postFx);
    REQUIRE(inFx != nullptr);
    REQUIRE(inPostFx != nullptr);
    CHECK(inFx->name == "In the FX tree");
    CHECK(inPostFx->name == "Post fader");

    // The unqualified overload still means the main FX tree, which is what
    // every existing caller has always asked it for.
    CHECK(tm.findDevicePath(kShared) == fx);
}

TEST_CASE("A section that does not hold the id answers with no path", "[devices][2600]") {
    resetState();

    auto& tm = TrackManager::getInstance();
    const auto trackId = tm.createTrack("Track");

    constexpr DeviceId kOnlyInFx = 3;
    tm.getTrack(trackId)->chain.fxChainElements.push_back(
        makeDeviceElement(deviceNamed(kOnlyInFx, "In the FX tree")));

    CHECK(tm.findDevicePath(kOnlyInFx, ChainSegment::Fx).isValid());
    CHECK_FALSE(tm.findDevicePath(kOnlyInFx, ChainSegment::PostFx).isValid());
    CHECK_FALSE(tm.findDevicePath(kOnlyInFx, ChainSegment::MixerAnalysis).isValid());
}

TEST_CASE("An attached provider carries the path it was given", "[devices][2600]") {
    // Stored rather than looked up again later: the id alone would send the
    // formatter to whichever device holds the same number in the FX tree.
    installDeviceParameterDisplayTextProviderFactory();

    auto device = deviceNamed(5, "Post fader");
    const auto path = ChainNodePath::postFxDevice(1, device.id);

    attachParameterTextProviders(device, path);

    REQUIRE(device.parameters[0].displayText != nullptr);
    CHECK(device.parameters[0].displayText->devicePath == path);
    CHECK(device.parameters[0].displayText->paramIndex == 2);
}

TEST_CASE("A device with no path is left formatting from its range", "[devices][2600]") {
    // Better than a provider that would resolve to something else: an empty
    // answer is what tells every caller to format from the parameter's range.
    installDeviceParameterDisplayTextProviderFactory();

    auto device = deviceNamed(5, "Nowhere");
    attachParameterTextProviders(device, ChainNodePath{});

    CHECK(device.parameters[0].displayText == nullptr);
}
