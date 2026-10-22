// A device built from the Sampler's parts rather than a copy of it (#3100): its own voice, its own
// plugin id and its own parameter slots.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "magda/daw/audio/plugins/MagdaSamplerPlugin.hpp"
#include "magda/daw/audio/plugins/sampler/SamplerEngine.hpp"
#include "magda/daw/audio/sampling/SamplerMedia.hpp"
#include "magda/daw/audio/sampling/SamplerModelEdits.hpp"
#include "magda/daw/core/SamplerDevices.hpp"
#include "magda/daw/core/TrackManager.hpp"

using namespace magda;
using namespace magda::daw::audio;
using Sampler = MagdaSamplerPlugin;

namespace {

juce::File writeWav(const juce::String& name, double seconds) {
    auto dir =
        juce::File(juce::SystemStats::getEnvironmentVariable(
                       "TMPDIR",
                       juce::File::getSpecialLocation(juce::File::tempDirectory).getFullPathName()))
            .getChildFile("magda_sampler_parts");
    dir.createDirectory();
    const auto file = dir.getChildFile(name + ".wav");
    const int frames = static_cast<int>(seconds * 44100.0);
    juce::AudioBuffer<float> buffer(1, frames);
    for (int i = 0; i < frames; ++i)
        buffer.setSample(0, i, 0.5f * std::sin(static_cast<float>(i) * 0.05f));
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    REQUIRE(stream != nullptr);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions()
                                                  .withSampleRate(44100.0)
                                                  .withNumChannels(1)
                                                  .withBitsPerSample(16));
    REQUIRE(writer != nullptr);
    REQUIRE(writer->writeFromAudioSampleBuffer(buffer, 0, frames));
    return file;
}

/// A voice that reads a constant instead of its zone's audio.
class ConstantVoice : public SamplerVoice {
  public:
    explicit ConstantVoice(const SamplerSynth& synth) : SamplerVoice(synth) {}
    static inline int reads = 0;

  protected:
    void readFrames(const SampleAudio&, const double*, int count, int, float* const* out,
                    int numChannels) override {
        ++reads;
        for (int ch = 0; ch < numChannels; ++ch)
            std::fill_n(out[ch], count, 0.25f);
    }
};

class Events : public sdk::MidiInput {
  public:
    std::vector<sdk::MidiEvent> events;
    int size() const override {
        return static_cast<int>(events.size());
    }
    const sdk::MidiEvent& event(int index) const override {
        return events[static_cast<size_t>(index)];
    }
    bool isAllNotesOff() const override {
        return false;
    }
};

constexpr const char* kDerivedId = "test.derived-sampler";

/// The derived device's slots: attack at 0 and the four markers at 2 to 5; nothing else.
sampler_devices::Layout derivedLayout() {
    sampler_devices::Layout layout;
    layout.slots.assign(Sampler::kNumParams, -1);
    layout.slots[Sampler::kAttack] = 0;
    layout.slots[Sampler::kSampleStart] = 2;
    layout.slots[Sampler::kSampleEnd] = 3;
    layout.slots[Sampler::kLoopStart] = 4;
    layout.slots[Sampler::kLoopEnd] = 5;
    return layout;
}

ChainNodePath addDerivedDevice(const juce::String& pluginId) {
    auto& tracks = TrackManager::getInstance();
    tracks.clearAllTracks();
    const auto trackId = tracks.createTrack("Derived", TrackType::Media);
    DeviceInfo device;
    device.name = "Derived";
    device.pluginId = pluginId;
    device.format = PluginFormat::Internal;
    device.isInstrument = true;
    for (int index = 0; index < 6; ++index) {
        ParameterInfo info;
        info.paramIndex = index;
        info.minValue = 0.0f;
        info.maxValue = 300.0f;
        info.defaultValue = index == 0 ? 0.01f : 0.0f;
        info.currentValue = info.defaultValue;
        device.parameters.push_back(info);
    }
    const auto deviceId = tracks.addDeviceToTrack(trackId, device);
    REQUIRE(deviceId != INVALID_DEVICE_ID);
    return ChainNodePath::topLevelDevice(trackId, deviceId);
}

float slotValue(const ChainNodePath& path, int slot) {
    const auto* device = TrackManager::getInstance().getDeviceInChainByPath(path);
    REQUIRE(device != nullptr);
    return device->findParameterByIndex(slot)->currentValue;
}

}  // namespace

TEST_CASE("A device's own voice reads its zones through the Sampler's engine",
          "[sampler][parts][3100]") {
    SamplerEngine engine(
        2, [](const SamplerSynth& synth) { return std::make_unique<ConstantVoice>(synth); });
    engine.prepare(44100.0);
    SamplerZoneSettings zone;
    zone.source = writeWav("parts-zone", 1.0).getFullPathName();
    zone.region.end = 1.0;
    engine.setZones({zone}, true);
    REQUIRE(engine.zoneMap().zones.front().hasAudio());

    Events in;
    in.events.push_back(sdk::MidiEvent::noteOn(1, 60, 127, 0));
    const DeviceMidiInput midi(in, 44100.0);
    juce::AudioBuffer<float> audio(2, 256);
    audio.clear();
    ConstantVoice::reads = 0;
    int settingsAsked = 0;
    engine.process(audio, &midi, 256, [&settingsAsked] {
        ++settingsAsked;
        SamplerVoiceSettings settings;
        settings.velocityAmount = 0.0f;
        settings.amp = {0.001f, 0.1f, 1.0f, 0.1f};
        return settings;
    });

    CHECK(settingsAsked == 1);
    CHECK(ConstantVoice::reads > 0);
    // Past the 1 ms attack the voice plays what it read, on both channels.
    CHECK(audio.getSample(0, 200) == Catch::Approx(0.25f));
    CHECK(audio.getSample(1, 200) == Catch::Approx(0.25f));
    CHECK(engine.playbackZone() == 0);

    // Nothing ringing and nothing arriving: no settings asked, nothing rendered.
    SamplerEngine idle;
    juce::AudioBuffer<float> silent(2, 64);
    silent.clear();
    idle.process(silent, nullptr, 64, [&settingsAsked] {
        ++settingsAsked;
        return SamplerVoiceSettings{};
    });
    CHECK(settingsAsked == 1);
}

TEST_CASE("Zone edits and imports write a registered device's own slots",
          "[sampler][parts][model-authority][3100]") {
    sampler_devices::registerDevice(kDerivedId, derivedLayout());
    REQUIRE(sampler_devices::isSampler(kDerivedId));
    const auto path = addDerivedDevice(kDerivedId);
    const auto file = writeWav("parts-load", 2.0);

    REQUIRE(sampler_edits::loadSample(path, file));
    CHECK(slotValue(path, 3) == Catch::Approx(2.0f));
    CHECK(slotValue(path, 5) == Catch::Approx(2.0f));
    CHECK(slotValue(path, 1) == Catch::Approx(0.0f));

    auto zones = sampler_edits::zones(path);
    REQUIRE(zones.size() == 1);
    CHECK(zones.front().region.end == Catch::Approx(2.0));

    // Collect and relocate walk it as they walk the Sampler.
    const auto media = modelSamplerMedia();
    CHECK(std::ranges::any_of(media,
                              [&file](const auto& reference) { return reference.source == file; }));

    const auto sfz = file.getSiblingFile("parts.sfz");
    sfz.replaceWithText("<region> sample=parts-load.wav ampeg_attack=0.4 ampeg_release=2\n");
    const auto imported = sampler_edits::importInstrument(path, sfz);
    REQUIRE(imported.error.isEmpty());
    CHECK(slotValue(path, 0) == Catch::Approx(0.4f));

    TrackManager::getInstance().clearAllTracks();
}

TEST_CASE("A device not built on the Sampler is not written as one", "[sampler][parts][3100]") {
    const auto path = addDerivedDevice("test.not-a-sampler");
    CHECK_FALSE(sampler_devices::isSampler("test.not-a-sampler"));
    CHECK_FALSE(sampler_edits::loadSample(path, writeWav("parts-refused", 0.5)));
    CHECK(sampler_edits::zones(path).empty());
    TrackManager::getInstance().clearAllTracks();
}
