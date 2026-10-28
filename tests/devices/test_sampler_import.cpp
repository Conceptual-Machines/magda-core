// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

// SFZ and DecentSampler instruments into the sampler's zone map (#3099).

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "magda/daw/audio/plugins/MagdaSamplerPlugin.hpp"
#include "magda/daw/audio/sampling/SamplerImport.hpp"
#include "magda/daw/audio/sampling/SamplerMedia.hpp"
#include "magda/daw/audio/sampling/SamplerModelEdits.hpp"
#include "magda/daw/core/DeviceState.hpp"
#include "magda/daw/core/TrackManager.hpp"

using namespace magda;
using Sampler = magda::daw::audio::MagdaSamplerPlugin;
namespace si = magda::sampler_import;

namespace {

juce::File freshDir(const juce::String& name) {
    auto dir =
        juce::File(juce::SystemStats::getEnvironmentVariable(
                       "TMPDIR",
                       juce::File::getSpecialLocation(juce::File::tempDirectory).getFullPathName()))
            .getChildFile("magda_sampler_import")
            .getChildFile(name);
    dir.deleteRecursively();
    dir.createDirectory();
    return dir;
}

/// One second of a quiet sine at 44.1 kHz, its metadata as given.
juce::File writeWav(const juce::File& file, const juce::StringPairArray& metadata = {}) {
    constexpr int kFrames = 44100;
    juce::AudioBuffer<float> buffer(1, kFrames);
    for (int i = 0; i < kFrames; ++i)
        buffer.setSample(0, i, 0.25f * std::sin(static_cast<float>(i) * 0.06f));
    file.getParentDirectory().createDirectory();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(new juce::FileOutputStream(file), 44100.0, 1, 16, metadata, 0));
    REQUIRE(writer != nullptr);
    REQUIRE(writer->writeFromAudioSampleBuffer(buffer, 0, kFrames));
    return file;
}

const daw::audio::SamplerZoneSettings* zoneFor(const si::Instrument& instrument,
                                               const juce::String& fileName) {
    for (const auto& zone : instrument.zones)
        if (juce::File(zone.source).getFileName() == fileName)
            return &zone;
    return nullptr;
}

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

}  // namespace

TEST_CASE("SFZ note names put C4 at 60", "[sampler][import][3099]") {
    CHECK(si::sfzNote("c4") == 60);
    CHECK(si::sfzNote("C4") == 60);
    CHECK(si::sfzNote("c#4") == 61);
    CHECK(si::sfzNote("eb3") == 51);
    CHECK(si::sfzNote("bb3") == 58);
    CHECK(si::sfzNote("c-1") == 0);
    CHECK(si::sfzNote("72") == 72);
    CHECK_FALSE(si::sfzNote("h3").has_value());
    CHECK_FALSE(si::sfzNote("200").has_value());
}

TEST_CASE("An SFZ's regions become zones with what their headers set", "[sampler][import][3099]") {
    const auto dir = freshDir("sfz");
    writeWav(dir.getChildFile("samples/Piano C3.wav"));
    writeWav(dir.getChildFile("samples/Piano C4 soft.wav"));
    writeWav(dir.getChildFile("samples/Piano C4 loud.wav"));
    juce::StringPairArray looped;
    looped.set("NumSampleLoops", "1");
    looped.set("Loop0Start", "1000");
    looped.set("Loop0End", "20999");
    looped.set("Loop0Type", "0");
    writeWav(dir.getChildFile("samples/Pad.wav"), looped);
    writeWav(dir.getChildFile("samples/Release.wav"));

    const auto sfz = dir.getChildFile("Piano.sfz");
    sfz.replaceWithText(R"(// A test instrument
#define $SOFT 63
<control> default_path=samples/
<global> ampeg_release=0.8 ampeg_sustain=50 /* the whole instrument */
<group> lovel=1 hivel=$SOFT xfin_lovel=1 xfin_hivel=11
<region> sample=Piano C4 soft.wav lokey=c4 hikey=c5 pitch_keycenter=c4 volume=-6
<group> lovel=64 hivel=127 seq_length=2
<region> sample=Piano C4 loud.wav key=60 tune=-20 transpose=1 offset=4410 end=22049
<region> sample=Piano C3.wav lokey=36 hikey=59 pitch_keycenter=48 amp_velcurve_64=0.5 pan=10
<group> trigger=release
<region> sample=Release.wav
<group>
<region> sample=Pad.wav lokey=0 hikey=35 pitch_keycenter=sample cutoff=800 fil_type=hpf_4p
<region> sample=Missing.wav key=100
<curve> v000=0
)");

    const auto instrument = si::importSfz(sfz);
    REQUIRE(instrument.error.isEmpty());
    REQUIRE(instrument.zones.size() == 4);

    const auto* soft = zoneFor(instrument, "Piano C4 soft.wav");
    REQUIRE(soft != nullptr);
    CHECK(soft->zones.keyLow == 60);
    CHECK(soft->zones.keyHigh == 72);
    CHECK(soft->zones.velocityHigh == 63);
    CHECK(soft->zones.velocityFadeLow == 10);
    CHECK(soft->gainDb == Catch::Approx(-6.0f));
    CHECK_FALSE(soft->zones.roundRobin);

    const auto* loud = zoneFor(instrument, "Piano C4 loud.wav");
    REQUIRE(loud != nullptr);
    CHECK(loud->zones.keyLow == 60);
    CHECK(loud->zones.keyHigh == 60);
    CHECK(loud->rootNote == 60);
    CHECK(loud->zones.velocityLow == 64);
    CHECK(loud->tuneCents == Catch::Approx(80.0f));
    CHECK(loud->region.start == Catch::Approx(0.1));
    CHECK(loud->region.end == Catch::Approx(0.5));
    CHECK(loud->zones.roundRobin);

    const auto* c3 = zoneFor(instrument, "Piano C3.wav");
    REQUIRE(c3 != nullptr);
    CHECK(c3->rootNote == 48);
    CHECK(c3->region.end == Catch::Approx(1.0));

    // No loop_mode: the file's own loop plays, its last frame inclusive.
    const auto* pad = zoneFor(instrument, "Pad.wav");
    REQUIRE(pad != nullptr);
    CHECK(pad->region.loop);
    CHECK(pad->region.loopStart == Catch::Approx(1000.0 / 44100.0));
    CHECK(pad->region.loopEnd == Catch::Approx(21000.0 / 44100.0));

    CHECK(instrument.parameters.at(Sampler::kRelease) == Catch::Approx(0.8f));
    CHECK(instrument.parameters.at(Sampler::kSustain) == Catch::Approx(0.5f));
    CHECK(instrument.parameters.at(Sampler::kFilterType) == Catch::Approx(1.0f));
    CHECK(instrument.parameters.at(Sampler::kFilterMode) == Catch::Approx(2.0f));
    CHECK(instrument.parameters.at(Sampler::kFilterPoles) == Catch::Approx(1.0f));
    CHECK(instrument.parameters.at(Sampler::kFilterCutoff) == Catch::Approx(800.0f));

    CHECK(instrument.dropped.contains("velocity curves"));
    CHECK(instrument.dropped.contains("opcode pan"));
    CHECK(instrument.dropped.contains("regions with trigger=release"));
    CHECK(instrument.dropped.contains("<curve> sections"));
    CHECK(instrument.dropped.contains("1 missing sample"));
}

TEST_CASE("An SFZ whose regions disagree on a device-wide value keeps the first",
          "[sampler][import][3099]") {
    const auto dir = freshDir("sfz-conflict");
    writeWav(dir.getChildFile("a.wav"));
    writeWav(dir.getChildFile("b.wav"));
    const auto sfz = dir.getChildFile("x.sfz");
    sfz.replaceWithText("<region> sample=a.wav ampeg_attack=0.2\n"
                        "<region> sample=b.wav ampeg_attack=0.5\n");

    const auto instrument = si::importSfz(sfz);
    REQUIRE(instrument.zones.size() == 2);
    CHECK(instrument.parameters.at(Sampler::kAttack) == Catch::Approx(0.2f));
    CHECK(instrument.dropped.contains(
        "ampeg_attack varying by region (the first region's are used)"));
}

TEST_CASE("An SFZ with no playable region says why", "[sampler][import][3099]") {
    const auto sfz = freshDir("sfz-empty").getChildFile("x.sfz");
    sfz.replaceWithText("<region> sample=*sine\n");
    const auto instrument = si::importSfz(sfz);
    CHECK(instrument.zones.empty());
    CHECK(instrument.error.isNotEmpty());
    CHECK(instrument.dropped.contains("generated waveforms (*sine)"));
}

TEST_CASE("A DecentSampler preset's samples become zones", "[sampler][import][3099]") {
    const auto dir = freshDir("dspreset");
    writeWav(dir.getChildFile("Samples/low.wav"));
    writeWav(dir.getChildFile("Samples/high.wav"));
    const auto preset = dir.getChildFile("Keys.dspreset");
    preset.replaceWithText(R"(<?xml version="1.0" encoding="UTF-8"?>
<DecentSampler minVersion="1.0.0">
  <ui width="812" height="375"/>
  <groups attack="0.01" release="1.5" volume="-3dB">
    <group seqMode="round_robin" tuning="0.5">
      <sample path="Samples/low.wav" rootNote="48" loNote="0" hiNote="59" loVel="1" hiVel="100"
              loopEnabled="true" loopStart="100" loopEnd="4410"/>
      <sample path="Samples/high.wav" rootNote="72" loNote="60" hiNote="127" volume="0.5"
              start="441" pan="20"/>
      <sample path="Samples/rel.wav" rootNote="60" trigger="release"/>
    </group>
  </groups>
  <effects>
    <effect type="lowpass" frequency="1200" resonance="0.7"/>
    <effect type="reverb" wetLevel="0.3"/>
  </effects>
  <midi><cc number="1"/></midi>
</DecentSampler>
)");

    const auto instrument = si::importDecentSampler(preset);
    REQUIRE(instrument.error.isEmpty());
    REQUIRE(instrument.zones.size() == 2);

    const auto* low = zoneFor(instrument, "low.wav");
    REQUIRE(low != nullptr);
    CHECK(low->rootNote == 48);
    CHECK(low->zones.keyHigh == 59);
    CHECK(low->zones.velocityHigh == 100);
    CHECK(low->zones.roundRobin);
    CHECK(low->tuneCents == Catch::Approx(50.0f));
    CHECK(low->gainDb == Catch::Approx(-3.0f));
    CHECK(low->region.loop);
    CHECK(low->region.loopEnd == Catch::Approx(0.1));

    const auto* high = zoneFor(instrument, "high.wav");
    REQUIRE(high != nullptr);
    CHECK(high->zones.keyLow == 60);
    CHECK(high->gainDb == Catch::Approx(-6.0206f).margin(0.001));
    CHECK(high->region.start == Catch::Approx(0.01));
    CHECK_FALSE(high->region.loop);

    CHECK(instrument.parameters.at(Sampler::kAttack) == Catch::Approx(0.01f));
    CHECK(instrument.parameters.at(Sampler::kRelease) == Catch::Approx(1.5f));
    CHECK(instrument.parameters.at(Sampler::kFilterType) == Catch::Approx(1.0f));
    CHECK(instrument.parameters.at(Sampler::kFilterMode) == Catch::Approx(0.0f));
    CHECK(instrument.parameters.at(Sampler::kFilterCutoff) == Catch::Approx(1200.0f));
    CHECK(instrument.parameters.at(Sampler::kFilterResonance) == Catch::Approx(0.0f));

    CHECK(instrument.dropped.contains("attribute pan"));
    CHECK(instrument.dropped.contains("samples with trigger=release"));
    CHECK(instrument.dropped.contains("effect reverb"));
    CHECK(instrument.dropped.contains("<midi>"));
}

TEST_CASE("Importing an instrument authors its zones, parameters and file on the model",
          "[sampler][import][model-authority][3099]") {
    const auto dir = freshDir("model");
    writeWav(dir.getChildFile("a.wav"));
    writeWav(dir.getChildFile("b.wav"));
    const auto sfz = dir.getChildFile("x.sfz");
    sfz.replaceWithText("<global> ampeg_release=2\n"
                        "<region> sample=a.wav hikey=59\n"
                        "<region> sample=b.wav lokey=60\n");
    const auto path = addSampler();
    auto& tracks = TrackManager::getInstance();
    tracks.setDeviceParameterValue(path, Sampler::kFilterType, 3.0f);

    const auto imported = sampler_edits::importInstrument(path, sfz);
    REQUIRE(imported.error.isEmpty());

    CHECK(sampler_edits::zones(path).size() == 2);
    const auto* device = tracks.getDeviceInChainByPath(path);
    REQUIRE(device != nullptr);
    CHECK(device->findParameterByIndex(Sampler::kRelease)->currentValue == Catch::Approx(2.0f));
    // What the instrument leaves unset is back at its default, not the last patch's.
    CHECK(device->findParameterByIndex(Sampler::kFilterType)->currentValue == Catch::Approx(0.0f));
    const auto doc = device_state::decode(device->pluginState);
    REQUIRE(doc.has_value());
    CHECK(doc->root.props[Sampler::StateIDs::instrument].toString() == sfz.getFullPathName());

    // Collect and relocate see the instrument file beside its samples.
    const auto media = modelSamplerMedia();
    CHECK(std::ranges::any_of(media,
                              [&sfz](const auto& reference) { return reference.source == sfz; }));

    // A newly loaded sample is no longer that instrument.
    REQUIRE(sampler_edits::loadSample(path, dir.getChildFile("a.wav")));
    const auto reloaded = device_state::decode(tracks.getDeviceInChainByPath(path)->pluginState);
    CHECK_FALSE(reloaded->root.props.contains(Sampler::StateIDs::instrument));

    tracks.clearAllTracks();
}

TEST_CASE("SFZ layers a player switches to are left out, the defaults kept",
          "[sampler][import][3099]") {
    const auto dir = freshDir("sfz-layers");
    for (const auto* name : {"cc-low", "cc-high", "prog0", "prog1", "sw-a", "sw-b", "held"})
        writeWav(dir.getChildFile(juce::String(name) + ".wav"));
    dir.getChildFile("Data").createDirectory();
    dir.getChildFile("Data/regions.txt")
        .replaceWithText("<region> sample=prog0.wav loprog=0 hiprog=0\n"
                         "<region> sample=prog1.wav loprog=1 hiprog=1\n");
    const auto sfz = dir.getChildFile("x.sfz");
    // A `//***` banner is a line comment, and an #include may sit mid-line.
    sfz.replaceWithText("//***************\n"
                        "#define $ART 100\n"
                        "<control> set_cc$ART=90\n"
                        "<group> hicc$ART=63 group_volume=-3 <region> sample=cc-low.wav\n"
                        "<group> locc$ART=64 <region> sample=cc-high.wav\n"
                        "<group> #include \"Data/regions.txt\"\n"
                        "<group> sw_last=24 <region> sample=sw-a.wav\n"
                        "<group> sw_last=25 <region> sample=sw-b.wav\n"
                        "<group> sw_down=26 <region> sample=held.wav\n");

    const auto instrument = si::importSfz(sfz);
    REQUIRE(instrument.error.isEmpty());
    juce::StringArray played;
    for (const auto& zone : instrument.zones)
        played.add(juce::File(zone.source).getFileNameWithoutExtension());
    played.sort(false);
    CHECK(played.joinIntoString(",") == "cc-high,prog0,sw-a");
    CHECK(instrument.dropped.contains("layers switched by MIDI CC (the defaults play)"));
    CHECK(instrument.dropped.contains("layers on other MIDI programs"));
    CHECK(instrument.dropped.contains("keyswitched articulations (the default plays)"));
    CHECK(instrument.dropped.contains("layers played while a key is held"));
}

TEST_CASE("SFZ v2 header volumes add to the region's", "[sampler][import][3099]") {
    const auto dir = freshDir("sfz-volume");
    writeWav(dir.getChildFile("a.wav"));
    const auto sfz = dir.getChildFile("x.sfz");
    sfz.replaceWithText("<global> global_volume=-6 <group> group_volume=-3 "
                        "<region> sample=a.wav volume=1\n");
    const auto instrument = si::importSfz(sfz);
    REQUIRE(instrument.zones.size() == 1);
    CHECK(instrument.zones.front().gainDb == Catch::Approx(-8.0f));
}
