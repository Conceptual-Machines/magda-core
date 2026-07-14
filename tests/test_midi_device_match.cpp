#include <juce_audio_devices/juce_audio_devices.h>

#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/audio/midi/MidiDeviceMatch.hpp"

namespace magda::midi {
namespace {

juce::MidiDeviceInfo makeDev(const juce::String& name, const juce::String& id) {
    juce::MidiDeviceInfo d;
    d.name = name;
    d.identifier = id;
    return d;
}

}  // namespace

TEST_CASE("matches: exact identifier wins", "[midi][match]") {
    REQUIRE(matches("id-123", "id-123", "Launchkey"));
    REQUIRE(!matches("id-999", "id-123", "Launchkey"));
}

TEST_CASE("matches: case-insensitive name fallback", "[midi][match]") {
    REQUIRE(matches("Launchkey", "id-123", "launchkey"));
    REQUIRE(matches("LAUNCHKEY", "id-123", "Launchkey"));
}

TEST_CASE("matches: empty key never matches", "[midi][match]") {
    REQUIRE(!matches("", "id-123", "Launchkey"));
    REQUIRE(!matches("", "", ""));
}

TEST_CASE("matches: empty name is not a match by name", "[midi][match]") {
    REQUIRE(!matches("Launchkey", "id-123", ""));
}

TEST_CASE("matchedByNameOnly: true when name matches but identifier differs", "[midi][match]") {
    REQUIRE(matchedByNameOnly("Launchkey", "different-id", "Launchkey"));
}

TEST_CASE("matchedByNameOnly: false when identifier matches (even if name also does)",
          "[midi][match]") {
    REQUIRE(!matchedByNameOnly("id-123", "id-123", "Launchkey"));
}

TEST_CASE("matchedByNameOnly: false when nothing matches", "[midi][match]") {
    REQUIRE(!matchedByNameOnly("other", "id-123", "Launchkey"));
}

TEST_CASE("resolve: prefers identifier match over name match", "[midi][resolve]") {
    juce::Array<juce::MidiDeviceInfo> devices;
    devices.add(makeDev("Launchkey", "id-A"));
    devices.add(makeDev("Other", "stored-id"));

    auto picked = resolve(devices, "stored-id");
    REQUIRE(picked.has_value());
    REQUIRE(picked->identifier == "stored-id");
    REQUIRE(picked->name == "Other");
}

TEST_CASE("resolve: falls back to name when no identifier match", "[midi][resolve]") {
    juce::Array<juce::MidiDeviceInfo> devices;
    devices.add(makeDev("Launchkey", "id-A"));
    devices.add(makeDev("Other", "id-B"));

    auto picked = resolve(devices, "launchkey");  // case-insensitive
    REQUIRE(picked.has_value());
    REQUIRE(picked->name == "Launchkey");
    REQUIRE(picked->identifier == "id-A");
}

TEST_CASE("resolve: returns nullopt when nothing matches", "[midi][resolve]") {
    juce::Array<juce::MidiDeviceInfo> devices;
    devices.add(makeDev("Launchkey", "id-A"));

    REQUIRE(!resolve(devices, "nothing-like-this").has_value());
    REQUIRE(!resolve(devices, "").has_value());
}

TEST_CASE("sameMidiHardware: identical names match, empty names never do", "[midi][sendback]") {
    REQUIRE(sameMidiHardware("IAC Driver Bus 1", "IAC Driver Bus 1"));
    REQUIRE(sameMidiHardware("monologue", "Monologue"));
    REQUIRE(!sameMidiHardware("", "monologue"));
    REQUIRE(!sameMidiHardware("monologue", ""));
}

TEST_CASE("sameMidiHardware: direction words are dropped", "[midi][sendback]") {
    REQUIRE(sameMidiHardware("monologue MIDI IN", "monologue MIDI OUT"));
    REQUIRE(sameMidiHardware("Device Input", "Device Output"));
}

TEST_CASE("sameMidiHardware: multi-port interfaces only match the same-numbered port",
          "[midi][sendback]") {
    REQUIRE(sameMidiHardware("MIDISPORT IN 2", "MIDISPORT OUT 2"));
    REQUIRE(!sameMidiHardware("MIDISPORT IN 2", "MIDISPORT OUT 1"));
}

TEST_CASE("sameMidiHardware: asymmetric per-port naming matches on the first two words",
          "[midi][sendback]") {
    REQUIRE(sameMidiHardware("Digitakt MIDI In-Port", "Digitakt MIDI Out-Port"));
}

TEST_CASE("sameMidiHardware: a shared brand prefix alone never matches", "[midi][sendback]") {
    // Unrelated same-brand controllers must NOT be classified as the
    // sendback port (they would be silently dropped from All-Inputs).
    REQUIRE(!sameMidiHardware("Arturia BeatStep Pro", "Arturia MicroFreak"));
    // The deliberate cost: single-word-model ports whose second word is a
    // port function no longer match either.
    REQUIRE(!sameMidiHardware("monologue KBD/KNOB", "monologue SOUND"));
}

}  // namespace magda::midi
