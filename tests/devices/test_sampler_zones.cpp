// The sampler's zone map (#3098): mapping files by name, and the device restoring and playing
// a document's zones.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "DeviceTestBlock.hpp"
#include "TestDeviceMidiBuffer.hpp"
#include "plugins/MagdaSamplerPlugin.hpp"
#include "plugins/SamplerZones.hpp"

using namespace magda::daw::audio;
using Sampler = MagdaSamplerPlugin;

namespace {

/// @p seconds of a constant @p level, so which zone sounded reads off the output.
juce::File writeLevelWav(const juce::String& name, float level, double seconds = 0.5) {
    constexpr double kRate = 44100.0;
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("magda_sampler_zones");
    dir.createDirectory();
    const auto file = dir.getChildFile(name + ".wav");
    juce::AudioBuffer<float> buffer(1, static_cast<int>(seconds * kRate));
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample(0, i, level);
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(new juce::FileOutputStream(file), kRate, 1, 24, {}, 0));
    REQUIRE(writer != nullptr);
    REQUIRE(writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()));
    return file;
}

magda::sdk::StateNode zoneNode(const juce::File& file, int low, int high, double tune = 0.0) {
    magda::sdk::StateNode zone("zone");
    zone.setString("source", file.getFullPathName().toStdString());
    zone.setInt("rootNote", 60);
    zone.setInt("keyLow", low);
    zone.setInt("keyHigh", high);
    zone.setDouble("tune", tune);
    return zone;
}

/// The first sample @p sampler plays for @p note.
float firstFrame(Sampler& sampler, int note) {
    constexpr int kBlock = 256;
    sampler.prepare({.sampleRate = 44100.0, .maximumBlockSize = kBlock});
    magda::test::DeviceMidiBuffer midi;
    midi.events.push_back({juce::MidiMessage::noteOn(1, note, 1.0f).withTimeStamp(0.0), 0});
    juce::AudioBuffer<float> audio(1, kBlock);
    audio.clear();
    magda::test::DeviceMidiBuffer out;
    magda::test::DeviceTestBlock block(audio);
    block.context.midiIn = &midi;
    block.context.midiOut = &out;
    sampler.process(block.context);
    sampler.reset();
    return audio.getSample(0, 200);  // past the 1 ms attack
}

}  // namespace

TEST_CASE("A file's name names its note", "[devices][sampler][3098]") {
    CHECK(noteFromFileName("Piano_C3") == 60);
    CHECK(noteFromFileName("Piano A#2") == 58);
    CHECK(noteFromFileName("Cello-Db4-ff") == 73);
    CHECK(noteFromFileName("Bass C-1") == 12);
    CHECK(noteFromFileName("kick 36") == 36);
    // A number beside a note name is a take, not a key.
    CHECK(noteFromFileName("Piano_C3_02") == 60);
    CHECK_FALSE(noteFromFileName("Bassline").has_value());
    CHECK_FALSE(noteFromFileName("Take 400").has_value());
}

TEST_CASE("Mapping by name gives each note its share of the keyboard", "[devices][sampler][3098]") {
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    const auto mapped = mapSamplesByName(
        {dir.getChildFile("x_C3.wav"), dir.getChildFile("x_C4.wav"), dir.getChildFile("hit.wav")});
    REQUIRE(mapped.size() == 3);
    CHECK(mapped[0].zone.zones.keyLow == 0);
    CHECK(mapped[0].zone.zones.keyHigh == 65);
    CHECK(mapped[1].zone.rootNote == 72);
    CHECK(mapped[1].zone.zones.keyLow == 66);
    CHECK(mapped[1].zone.zones.keyHigh == 72);
    // A file naming no note goes on the next key up.
    CHECK(mapped[2].zone.rootNote == 73);
    CHECK(mapped[2].zone.zones.keyHigh == 127);
}

TEST_CASE("A document's zones play where their ranges put them", "[devices][sampler][3098]") {
    const auto low = writeLevelWav("zones_low", 0.25f);
    const auto high = writeLevelWav("zones_high", 0.5f);
    magda::sdk::StateNode state;
    state.addChild(zoneNode(low, 0, 59));
    state.addChild(zoneNode(high, 60, 127));

    Sampler sampler;
    REQUIRE(sampler.restoreState(state).ok);
    REQUIRE(sampler.hasZoneList());
    REQUIRE(sampler.zoneMap().zones.size() == 2);

    CHECK(firstFrame(sampler, 48) == Catch::Approx(0.25f).margin(1e-4));
    CHECK(firstFrame(sampler, 72) == Catch::Approx(0.5f).margin(1e-4));
}

TEST_CASE("Editing one zone reads no file again", "[devices][sampler][3098]") {
    const auto low = writeLevelWav("edit_low", 0.25f);
    const auto high = writeLevelWav("edit_high", 0.5f);
    magda::sdk::StateNode state;
    state.addChild(zoneNode(low, 0, 59));
    state.addChild(zoneNode(high, 60, 127));
    Sampler sampler;
    REQUIRE(sampler.restoreState(state).ok);
    const auto lowAudio = sampler.zoneMap().zones[0].audio;
    const auto highAudio = sampler.zoneMap().zones[1].audio;

    magda::sdk::StateNode edited;
    edited.addChild(zoneNode(low, 0, 59));
    edited.addChild(zoneNode(high, 60, 127, 100.0));
    REQUIRE(sampler.restoreState(edited).ok);

    CHECK(sampler.zoneMap().zones[0].audio == lowAudio);
    CHECK(sampler.zoneMap().zones[1].audio == highAudio);
    CHECK(sampler.zoneMap().zones[1].settings.tuneCents == 100.0f);
}

TEST_CASE("A one-zone document is today's sampler", "[devices][sampler][3098]") {
    const auto file = writeLevelWav("one_zone", 0.25f);
    magda::sdk::StateNode state;
    state.setString("source", file.getFullPathName().toStdString());
    state.setInt("rootNote", 60);

    Sampler sampler;
    REQUIRE(sampler.restoreState(state).ok);
    CHECK_FALSE(sampler.hasZoneList());
    REQUIRE(sampler.zoneMap().zones.size() == 1);
    CHECK(sampler.zoneMap().zones[0].regionFromParameters);
    CHECK(sampler.getSampleFile() == file);
    CHECK(firstFrame(sampler, 30) == Catch::Approx(0.25f).margin(1e-4));
}
