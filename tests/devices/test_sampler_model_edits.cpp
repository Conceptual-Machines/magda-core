// A sampler's faceplate edits land on the model, not on the runtime (#2379).
//
// No audio engine in this binary: TrackManager skips the projection, so what is
// under test is exactly the model-side half — the document a save writes and a
// rebuild reads back.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "magda/daw/audio/plugins/MagdaSamplerPlugin.hpp"
#include "magda/daw/audio/sampling/SamplerModelEdits.hpp"
#include "magda/daw/core/DeviceState.hpp"
#include "magda/daw/core/TrackManager.hpp"

using namespace magda;
namespace ds = magda::device_state;
using Sampler = magda::daw::audio::MagdaSamplerPlugin;

namespace {

/// A sampler on a fresh track, with its parameters seeded the way the engine
/// sync seeds them once a processor exists — nothing else would put them there
/// in a binary with no engine, and a parameter write needs the entry.
ChainNodePath addSampler() {
    auto& tracks = TrackManager::getInstance();
    tracks.clearAllTracks();
    const auto trackId = tracks.createTrack("Sampler", TrackType::Media);

    DeviceInfo device;
    device.name = "Sampler";
    device.pluginId = Sampler::xmlTypeName;
    device.format = PluginFormat::Internal;
    device.isInstrument = true;

    const Sampler metadata;
    for (int index = 0; index < Sampler::kNumParams; ++index) {
        auto info = metadata.parameterInfo(index);
        info.paramIndex = index;
        info.currentValue = info.defaultValue;
        device.parameters.push_back(info);
    }

    const auto deviceId = tracks.addDeviceToTrack(trackId, device);
    REQUIRE(deviceId != INVALID_DEVICE_ID);
    return ChainNodePath::topLevelDevice(trackId, deviceId);
}

ds::Doc authoredDoc(const ChainNodePath& path) {
    const auto* device = TrackManager::getInstance().getDeviceInChainByPath(path);
    REQUIRE(device != nullptr);
    const auto doc = ds::decode(device->pluginState);
    REQUIRE(doc.has_value());
    return *doc;
}

float modelValue(const ChainNodePath& path, int index) {
    const auto* device = TrackManager::getInstance().getDeviceInChainByPath(path);
    REQUIRE(device != nullptr);
    const auto* param = device->findParameterByIndex(index);
    REQUIRE(param != nullptr);
    return param->currentValue;
}

juce::File scratchDir() {
    auto dir =
        juce::File(juce::SystemStats::getEnvironmentVariable(
                       "TMPDIR",
                       juce::File::getSpecialLocation(juce::File::tempDirectory).getFullPathName()))
            .getChildFile("magda_sampler_model_edits");
    dir.createDirectory();
    return dir;
}

/// A sine of @p seconds, so the marker span the load derives is a known number.
juce::File writeTestWav(double seconds, const juce::String& name = "sample") {
    constexpr double sampleRate = 44100.0;
    const int numSamples = static_cast<int>(seconds * sampleRate);
    juce::AudioBuffer<float> buffer(1, numSamples);
    for (int i = 0; i < numSamples; ++i)
        buffer.setSample(0, i,
                         0.5f * std::sin(static_cast<float>(i) * 440.0f * 6.2831853f / 44100.0f));

    const auto file = scratchDir().getNonexistentChildFile(name, ".wav");
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(new juce::FileOutputStream(file), sampleRate, 1, 16, {}, 0));
    REQUIRE(writer != nullptr);
    REQUIRE(writer->writeFromAudioSampleBuffer(buffer, 0, numSamples));
    writer.reset();
    return file;
}

}  // namespace

TEST_CASE("The loop switch and root note are authored on the model", "[sampler][model-authority]") {
    const auto path = addSampler();

    REQUIRE(sampler_edits::setLoopEnabled(path, true));
    REQUIRE(sampler_edits::setRootNote(path, 48));

    const auto doc = authoredDoc(path);
    CHECK(static_cast<bool>(doc.root.props[Sampler::StateIDs::loopEnabled]));
    CHECK(static_cast<int>(doc.root.props[Sampler::StateIDs::rootNote]) == 48);
    CHECK(doc.deviceType == Sampler::xmlTypeName);

    TrackManager::getInstance().clearAllTracks();
}

TEST_CASE("Loading a sample authors its path and its markers", "[sampler][model-authority]") {
    const auto path = addSampler();
    const auto file = writeTestWav(2.0);

    REQUIRE(sampler_edits::loadSample(path, file));

    const auto doc = authoredDoc(path);
    CHECK(doc.root.props[Sampler::StateIDs::source].toString() == file.getFullPathName());

    // The pad's trim is what makes a slice a slice, and it is a model parameter:
    // a load that only reached the runtime left the model on its defaults, so a
    // rebuild played the whole file (#2379).
    CHECK(modelValue(path, Sampler::kSampleStart) == Catch::Approx(0.0f));
    CHECK(modelValue(path, Sampler::kSampleEnd) == Catch::Approx(2.0f).margin(0.01));
    CHECK(modelValue(path, Sampler::kLoopStart) == Catch::Approx(0.0f));
    CHECK(modelValue(path, Sampler::kLoopEnd) == Catch::Approx(2.0f).margin(0.01));

    file.deleteFile();
    TrackManager::getInstance().clearAllTracks();
}

TEST_CASE("A newly chosen sample does not inherit the previous one's trim",
          "[sampler][model-authority]") {
    const auto path = addSampler();
    const auto first = writeTestWav(2.0);
    REQUIRE(sampler_edits::loadSample(path, first));

    auto& tracks = TrackManager::getInstance();
    tracks.setDeviceParameterValue(path, Sampler::kSampleStart, 0.5f);
    tracks.setDeviceParameterValue(path, Sampler::kSampleEnd, 1.5f);

    const auto second = writeTestWav(3.0);
    REQUIRE(sampler_edits::loadSample(path, second));

    CHECK(modelValue(path, Sampler::kSampleStart) == Catch::Approx(0.0f));
    CHECK(modelValue(path, Sampler::kSampleEnd) == Catch::Approx(3.0f).margin(0.01));

    first.deleteFile();
    second.deleteFile();
    tracks.clearAllTracks();
}

TEST_CASE("An unreadable sample is refused before anything is written",
          "[sampler][model-authority]") {
    const auto path = addSampler();
    REQUIRE(sampler_edits::setRootNote(path, 48));

    const auto notAudio = scratchDir().getNonexistentChildFile("not_audio", ".wav");
    notAudio.replaceWithText("this is not a wav file");

    CHECK_FALSE(sampler_edits::loadSample(path, notAudio));
    CHECK_FALSE(authoredDoc(path).root.props.contains(Sampler::StateIDs::source));

    notAudio.deleteFile();
    TrackManager::getInstance().clearAllTracks();
}

TEST_CASE("Several samples load as zones mapped by the notes in their names",
          "[sampler][model-authority][3098]") {
    const auto path = addSampler();
    const auto c3 = writeTestWav(0.25, "Piano_C3");
    const auto e3 = writeTestWav(0.25, "Piano_E3");
    const auto softG3 = writeTestWav(0.25, "Piano_G3_1");
    const auto loudG3 = writeTestWav(0.5, "Piano_G3_2");

    REQUIRE(sampler_edits::loadSamples(path, {loudG3, c3, softG3, e3}));

    const auto zones = sampler_edits::zones(path);
    REQUIRE(zones.size() == 4);
    CHECK(zones[0].source == c3.getFullPathName());
    CHECK(zones[0].rootNote == 60);
    CHECK(zones[0].zones.keyLow == 0);
    CHECK(zones[0].zones.keyHigh == 61);
    CHECK(zones[1].rootNote == 64);
    CHECK(zones[1].zones.keyLow == 62);
    CHECK(zones[1].zones.keyHigh == 65);
    // Two files on G3 stack as velocity layers, softest by name first.
    CHECK(zones[2].source == softG3.getFullPathName());
    CHECK(zones[2].zones.keyLow == 66);
    CHECK(zones[2].zones.keyHigh == 127);
    CHECK(zones[2].zones.velocityHigh == 63);
    CHECK(zones[3].source == loudG3.getFullPathName());
    CHECK(zones[3].zones.velocityLow == 64);
    CHECK(zones[3].region.end == Catch::Approx(0.5));

    // A list of zones is the document's children; the root names no file of its own.
    const auto doc = authoredDoc(path);
    CHECK(doc.root.children.size() == 4);
    CHECK(doc.root.props[Sampler::StateIDs::source].isVoid());

    TrackManager::getInstance().clearAllTracks();
}

TEST_CASE("A zone added to a one-zone sampler keeps the first zone's markers",
          "[sampler][model-authority][3098]") {
    const auto path = addSampler();
    const auto first = writeTestWav(1.0);
    REQUIRE(sampler_edits::loadSample(path, first));
    TrackManager::getInstance().setDeviceParameterValue(path, Sampler::kSampleEnd, 0.5f);
    REQUIRE(sampler_edits::setLoopEnabled(path, true));

    REQUIRE(sampler_edits::addZone(path, writeTestWav(0.25), 72));

    const auto zones = sampler_edits::zones(path);
    REQUIRE(zones.size() == 2);
    CHECK(zones[0].source == first.getFullPathName());
    CHECK(zones[0].region.end == Catch::Approx(0.5));
    CHECK(zones[0].region.loop);
    CHECK(zones[1].rootNote == 72);
    CHECK(zones[1].zones.keyLow == 72);
    CHECK(zones[1].zones.keyHigh == 72);

    TrackManager::getInstance().clearAllTracks();
}

TEST_CASE("A map edited down to one zone is the one-zone sampler again",
          "[sampler][model-authority][3098]") {
    const auto path = addSampler();
    const auto kept = writeTestWav(1.0);
    REQUIRE(sampler_edits::loadSamples(path, {writeTestWav(0.25, "Bass_C2"), kept}));
    auto zones = sampler_edits::zones(path);
    REQUIRE(zones.size() == 2);
    const int keptIndex = zones[0].source == kept.getFullPathName() ? 0 : 1;
    zones[static_cast<size_t>(keptIndex)].region.end = 0.75;
    zones[static_cast<size_t>(keptIndex)].zones.keyLow = 48;
    REQUIRE(sampler_edits::updateZone(path, keptIndex, zones[static_cast<size_t>(keptIndex)]));

    REQUIRE(sampler_edits::removeZone(path, 1 - keptIndex));

    const auto doc = authoredDoc(path);
    CHECK(doc.root.children.empty());
    CHECK(doc.root.props[Sampler::StateIDs::source].toString() == kept.getFullPathName());
    CHECK(static_cast<int>(doc.root.props["keyLow"]) == 48);
    CHECK(modelValue(path, Sampler::kSampleEnd) == Catch::Approx(0.75f));

    TrackManager::getInstance().clearAllTracks();
}
