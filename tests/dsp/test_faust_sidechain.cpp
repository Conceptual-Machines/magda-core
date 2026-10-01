#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "DeviceTestBlock.hpp"
#include "core/DeviceState.hpp"
#include "magda/daw/audio/faust/FaustModelEdits.hpp"
#include "magda/daw/audio/plugins/FaustPlugin.hpp"
#include "magda/daw/core/ParameterUtils.hpp"
#include "magda/daw/core/TrackManager.hpp"

// A runtime Faust effect's audio sidechain key, declared by its source and fed through
// DeviceProcessContext::sidechain as the native engine feeds it (#2329; #2556).

namespace {

namespace audio = magda::daw::audio;
using Catch::Approx;

// Each source names "stdfaust.lib" so compile() does not import a library the binary lacks.
constexpr const char* kSidechainDsp = R"FAUST(
// stdfaust.lib
declare magda_sidechain "audio";
process(mainL, mainR, sideL, sideR) = mainL + sideL, mainR + sideR;
)FAUST";

/// One control with a range nothing defaults to, so stale metadata is obvious on sight.
constexpr const char* kNamedControlDsp = R"FAUST(
// stdfaust.lib
cutoff = hslider("Cutoff [unit:Hz] [idx:0]", 5000, 500, 9000, 1);
process = *(cutoff / 9000.0), *(cutoff / 9000.0);
)FAUST";

constexpr const char* kStereoDsp = R"FAUST(
// stdfaust.lib
process = _, _;
)FAUST";

constexpr const char* kMonoSidechainDsp = R"FAUST(
// stdfaust.lib
declare magda_sidechain "audio";
process(mainL, mainR, sidechain) = mainL + sidechain, mainR + sidechain;
)FAUST";

constexpr const char* kNineInputDsp = R"FAUST(
// stdfaust.lib
process(a, b, c, d, e, f, g, h, i) = a, b;
)FAUST";

/// Four inputs and no declaration: extra inputs are not a key on their own.
constexpr const char* kUndeclaredWideDsp = R"FAUST(
// stdfaust.lib
process(mainL, mainR, extraL, extraR) = mainL + extraL, mainR + extraR;
)FAUST";

constexpr int kBlockSize = 64;
constexpr double kSampleRate = 44100.0;

void load(audio::FaustPlugin& faust, const char* name, const char* source) {
    juce::String error;
    const bool loaded = faust.loadDspSource(name, source, error);
    INFO(error);
    REQUIRE(loaded);
}

void prepare(audio::FaustPlugin& faust) {
    faust.prepare({.sampleRate = kSampleRate, .maximumBlockSize = kBlockSize});
}

/// Main channels at 0.1 and 0.2, the key at 0.3 and 0.4, through one block.
juce::AudioBuffer<float> renderWithKey(audio::FaustPlugin& faust) {
    juce::AudioBuffer<float> buffer(2, kBlockSize);
    juce::AudioBuffer<float> key(2, kBlockSize);
    for (int channel = 0; channel < 2; ++channel) {
        std::fill_n(buffer.getWritePointer(channel), kBlockSize, 0.1f * (channel + 1));
        std::fill_n(key.getWritePointer(channel), kBlockSize, 0.1f * (channel + 3));
    }

    magda::test::DeviceTestBlock contextBlock(buffer, kBlockSize);
    auto& context = contextBlock.context;
    contextBlock.setSidechain(key);
    faust.process(context);
    return buffer;
}

}  // namespace

TEST_CASE("A declared key advertises and renders a stereo audio sidechain",
          "[faust][sidechain][2556]") {
    audio::FaustPlugin faust;
    load(faust, "Sidechain test", kSidechainDsp);

    const auto properties = faust.properties();
    CHECK(properties.sidechain.takesAudio());
    CHECK(properties.sidechain.channels == 2);
    CHECK(properties.inputChannelCount == 2);
    CHECK(properties.outputChannelCount == 2);

    prepare(faust);
    const auto rendered = renderWithKey(faust);
    CHECK(rendered.getSample(0, 0) == Approx(0.4f).margin(0.0001f));
    CHECK(rendered.getSample(1, 0) == Approx(0.6f).margin(0.0001f));
}

TEST_CASE("A three-input source advertises a mono sidechain", "[faust][sidechain][2556]") {
    audio::FaustPlugin faust;
    load(faust, "Mono sidechain test", kMonoSidechainDsp);

    const auto properties = faust.properties();
    CHECK(properties.sidechain.takesAudio());
    CHECK(properties.sidechain.channels == 1);
    CHECK(properties.inputChannelCount == 2);
}

TEST_CASE("Extra inputs without a declaration are not a key", "[faust][sidechain][2556]") {
    audio::FaustPlugin faust;
    load(faust, "Undeclared wide test", kUndeclaredWideDsp);

    const auto properties = faust.properties();
    CHECK_FALSE(properties.sidechain.declared());
    CHECK(properties.inputChannelCount == 4);
}

TEST_CASE("A runtime patch wider than scratch capacity fails safely", "[faust][sidechain][2556]") {
    audio::FaustPlugin faust;
    load(faust, "Sidechain test", kSidechainDsp);
    prepare(faust);
    const auto before = renderWithKey(faust);

    // Loaded after prepare(), so the input scratch is still sized for the previous patch.
    load(faust, "Wide input test", kNineInputDsp);
    CHECK(faust.properties().inputChannelCount == 9);
    const auto& diagnostics = faust.getLastRebindDiagnostics();
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics.front().containsIgnoreCase("reload"));

    auto buffer = before;
    magda::test::DeviceTestBlock contextBlock(buffer, kBlockSize);
    auto& context = contextBlock.context;
    faust.process(context);
    CHECK(buffer.getSample(0, 0) == Approx(before.getSample(0, 0)).margin(0.0001f));
}

TEST_CASE("A stereo patch swap removes the key and the model's routed source",
          "[faust][sidechain][2556]") {
    audio::FaustPlugin faust;
    load(faust, "Sidechain test", kSidechainDsp);
    REQUIRE(faust.properties().sidechain.takesAudio());
    load(faust, "Stereo test", kStereoDsp);
    CHECK_FALSE(faust.properties().sidechain.declared());
    CHECK(faust.properties().inputChannelCount == 2);

    // The model half: a key routed into a patch that no longer has one is cleared.
    auto& tracks = magda::TrackManager::getInstance();
    tracks.clearAllTracks();
    const auto sourceTrackId = tracks.createTrack("Sidechain source");
    const auto destinationTrackId = tracks.createTrack("Sidechain destination");

    magda::device_state::Doc doc;
    doc.deviceType = audio::FaustPlugin::xmlTypeName;
    doc.root.props.set(audio::kFaustDspSourceProperty, kSidechainDsp);
    doc.root.props.set(audio::kFaustDspNameProperty, "Sidechain test");
    magda::DeviceInfo saved;
    saved.name = "Faust";
    saved.format = magda::PluginFormat::Internal;
    saved.pluginId = audio::FaustPlugin::xmlTypeName;
    saved.pluginState = magda::device_state::encode(doc);
    saved.sidechainPort = {.kind = magda::SidechainPort::Kind::Audio, .channels = 2};
    saved.sidechain.type = magda::SidechainConfig::Type::Audio;
    saved.sidechain.sourceTrackId = sourceTrackId;

    const auto deviceId = tracks.addDeviceToTrack(destinationTrackId, saved);
    REQUIRE(deviceId != magda::INVALID_DEVICE_ID);
    const auto path = magda::ChainNodePath::topLevelDevice(destinationTrackId, deviceId);
    REQUIRE(tracks.getDeviceInChainByPath(path)->sidechain.isActive());

    juce::String error;
    REQUIRE(magda::faust_edits::loadSource(path, "Stereo test", kStereoDsp, error));
    const auto* device = tracks.getDeviceInChainByPath(path);
    REQUIRE(device != nullptr);
    CHECK_FALSE(device->sidechainPort.declared());
    CHECK_FALSE(device->sidechain.isActive());

    tracks.clearAllTracks();
}

TEST_CASE("A recompile is visible on the device's parameter metadata", "[faust][sidechain][2556]") {
    // The pool rebinds on every compile; a slot's name, range and unit follow the source.
    audio::FaustPlugin faust;
    load(faust, "Stereo test", kStereoDsp);
    load(faust, "Named control", kNamedControlDsp);

    const auto info = faust.parameterInfo(0);
    CHECK(faust.offersParameter(0));
    CHECK(info.name == juce::String("Cutoff"));
    CHECK(info.minValue == Approx(500.0f));
    CHECK(info.maxValue == Approx(9000.0f));
    CHECK(magda::ParameterUtils::normalizedToReal(0.5f, info) == Approx(4750.0f).margin(0.5f));
}
