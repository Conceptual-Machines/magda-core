#include <catch2/catch_test_macros.hpp>
#include <set>

#include "../magda/daw/audio/osc/OscAddress.hpp"

using namespace magda::osc;

namespace {

OscCommand parsed(const char* address) {
    auto command = parseOscAddress(address);
    REQUIRE(command.has_value());
    return *command;
}

void requireRejected(const char* address) {
    INFO("address: " << address);
    REQUIRE_FALSE(parseOscAddress(address).has_value());
}

}  // namespace

// ============================================================================
// The namespace a stock template speaks
// ============================================================================

TEST_CASE("Transport addresses parse", "[osc][address]") {
    REQUIRE(parsed("/magda/transport/play").kind == OscCommandKind::TransportPlay);
    REQUIRE(parsed("/magda/transport/stop").kind == OscCommandKind::TransportStop);
    REQUIRE(parsed("/magda/transport/record").kind == OscCommandKind::TransportRecord);
    REQUIRE(parsed("/magda/transport/loop").kind == OscCommandKind::TransportLoop);
    REQUIRE(parsed("/magda/transport/tempo").kind == OscCommandKind::TransportTempo);
    REQUIRE(parsed("/magda/transport/position").kind == OscCommandKind::TransportPosition);
}

TEST_CASE("Master addresses parse", "[osc][address]") {
    REQUIRE(parsed("/magda/master/volume").kind == OscCommandKind::MasterVolume);
    REQUIRE(parsed("/magda/master/pan").kind == OscCommandKind::MasterPan);
}

TEST_CASE("Track strip addresses carry a 1-based position", "[osc][address]") {
    const auto volume = parsed("/magda/track/1/volume");
    REQUIRE(volume.kind == OscCommandKind::TrackVolume);
    REQUIRE(volume.index == 1);
    REQUIRE(volume.subIndex == 0);

    REQUIRE(parsed("/magda/track/7/pan").kind == OscCommandKind::TrackPan);
    REQUIRE(parsed("/magda/track/7/pan").index == 7);
    REQUIRE(parsed("/magda/track/12/mute").kind == OscCommandKind::TrackMute);
    REQUIRE(parsed("/magda/track/12/solo").kind == OscCommandKind::TrackSolo);

    const auto send = parsed("/magda/track/3/send/2");
    REQUIRE(send.kind == OscCommandKind::TrackSend);
    REQUIRE(send.index == 3);
    REQUIRE(send.subIndex == 2);
}

TEST_CASE("Focused macro addresses parse", "[osc][address]") {
    const auto macro = parsed("/magda/focused/macro/16");
    REQUIRE(macro.kind == OscCommandKind::FocusedMacro);
    REQUIRE(macro.index == 16);
}

// ============================================================================
// What must not parse
// ============================================================================

TEST_CASE("Addresses outside the namespace are rejected", "[osc][address]") {
    requireRejected("/other/transport/play");
    requireRejected("/magda");
    requireRejected("/magda/transport");
    requireRejected("/magda/transport/rewind");
    requireRejected("/magda/master/width");
    requireRejected("/magda/focused/param/1");
    requireRejected("/magda/track/1/gain");
    requireRejected("");
    // No leading slash: not a well-formed OSC address, so not ours to accept.
    requireRejected("magda/transport/play");
}

TEST_CASE("Trailing components are rejected rather than ignored", "[osc][address]") {
    // A surface author who appends something we do not understand has a bug,
    // and silently applying the prefix would hide it behind working faders.
    requireRejected("/magda/transport/play/now");
    requireRejected("/magda/master/volume/1");
    requireRejected("/magda/track/1/volume/2");
    requireRejected("/magda/track/1/send");
    requireRejected("/magda/track/1/send/1/extra");
    requireRejected("/magda/focused/macro");
    requireRejected("/magda/focused/macro/1/2");
}

TEST_CASE("Wildcards never resolve to an index", "[osc][address]") {
    // OSC address patterns are part of the protocol. Reading '*' as a number
    // would let one message drive every strip at once.
    requireRejected("/magda/track/*/volume");
    requireRejected("/magda/track/?/volume");
    requireRejected("/magda/track/[1-8]/volume");
    requireRejected("/magda/track/{1,2}/volume");
    requireRejected("/magda/focused/macro/*");
}

TEST_CASE("Indices are strict decimals", "[osc][address]") {
    requireRejected("/magda/track/0/volume");   // 1-based, so 0 addresses nothing
    requireRejected("/magda/track/01/volume");  // one strip, one spelling
    requireRejected("/magda/track/-1/volume");
    requireRejected("/magda/track/1.0/volume");
    requireRejected("/magda/track/1x/volume");
    requireRejected("/magda/track/ 1/volume");
    requireRejected("/magda/track//volume");
    requireRejected("/magda/track/two/volume");
}

TEST_CASE("Out-of-range indices are rejected, not clamped", "[osc][address]") {
    // Clamping would land a misconfigured template's fader on a real track.
    REQUIRE(parseOscAddress("/magda/track/128/volume").has_value());
    requireRejected("/magda/track/129/volume");
    requireRejected("/magda/track/9999/volume");

    REQUIRE(parseOscAddress("/magda/track/1/send/8").has_value());
    requireRejected("/magda/track/1/send/9");

    REQUIRE(parseOscAddress("/magda/focused/macro/16").has_value());
    requireRejected("/magda/focused/macro/17");
}

// ============================================================================
// Argument conventions
// ============================================================================

TEST_CASE("Each kind declares how it reads its argument", "[osc][address]") {
    REQUIRE(argKindFor(OscCommandKind::TransportPlay) == OscArgKind::Trigger);
    REQUIRE(argKindFor(OscCommandKind::TransportStop) == OscArgKind::Trigger);
    REQUIRE(argKindFor(OscCommandKind::TransportRecord) == OscArgKind::Toggle);
    REQUIRE(argKindFor(OscCommandKind::TransportLoop) == OscArgKind::Toggle);
    REQUIRE(argKindFor(OscCommandKind::TrackMute) == OscArgKind::Toggle);
    REQUIRE(argKindFor(OscCommandKind::TrackSolo) == OscArgKind::Toggle);
    REQUIRE(argKindFor(OscCommandKind::TransportTempo) == OscArgKind::Bpm);
    REQUIRE(argKindFor(OscCommandKind::TransportPosition) == OscArgKind::Beats);
    REQUIRE(argKindFor(OscCommandKind::TrackVolume) == OscArgKind::Normalized);
    REQUIRE(argKindFor(OscCommandKind::TrackSend) == OscArgKind::Normalized);
    REQUIRE(argKindFor(OscCommandKind::MasterVolume) == OscArgKind::Normalized);
    REQUIRE(argKindFor(OscCommandKind::FocusedMacro) == OscArgKind::Normalized);
}

// ============================================================================
// Slot mapping
// ============================================================================

TEST_CASE("Every address owns a distinct slot", "[osc][address]") {
    // The coalescing table is only correct if two addresses never share a slot:
    // a collision would make one fader overwrite another's value.
    std::set<int> used;

    auto claim = [&used](const OscCommand& command) {
        const int slot = oscSlotIndex(command);
        INFO("slot " << slot);
        REQUIRE(slot >= 0);
        REQUIRE(slot < kOscSlotCount);
        REQUIRE(used.insert(slot).second);
        // And the drain must recover exactly what was addressed.
        REQUIRE(oscCommandForSlot(slot) == command);
    };

    for (int kind = 0; kind < kOscUnindexedSlots; ++kind)
        claim(OscCommand{static_cast<OscCommandKind>(kind), 0, 0});

    for (int macro = 1; macro <= kMaxMacroNumber; ++macro)
        claim(OscCommand{OscCommandKind::FocusedMacro, macro, 0});

    for (int track = 1; track <= kMaxTrackNumber; ++track) {
        claim(OscCommand{OscCommandKind::TrackVolume, track, 0});
        claim(OscCommand{OscCommandKind::TrackPan, track, 0});
        claim(OscCommand{OscCommandKind::TrackMute, track, 0});
        claim(OscCommand{OscCommandKind::TrackSolo, track, 0});
        for (int send = 1; send <= kMaxSendNumber; ++send)
            claim(OscCommand{OscCommandKind::TrackSend, track, send});
    }

    // Exactly as many slots as the table is sized for — no dead space, and
    // nothing addressable left outside it.
    REQUIRE(static_cast<int>(used.size()) == kOscSlotCount);
}

TEST_CASE("The relative seek addresses parse", "[osc][address]") {
    REQUIRE(parsed("/magda/transport/seek").kind == OscCommandKind::TransportSeekBeats);
    REQUIRE(parsed("/magda/transport/seek/bars").kind == OscCommandKind::TransportSeekBars);

    // A distance, not a position, and that is what keeps it off the coalescing
    // table: two presses of a rewind button are two bars.
    REQUIRE(argKindFor(OscCommandKind::TransportSeekBeats) == OscArgKind::Delta);
    REQUIRE(argKindFor(OscCommandKind::TransportSeekBars) == OscArgKind::Delta);

    // `bars` is a unit hanging off seek rather than a leaf of its own, so
    // neither half of it stands alone and no other transport address takes a
    // fourth component.
    REQUIRE_FALSE(parseOscAddress("/magda/transport/bars").has_value());
    REQUIRE_FALSE(parseOscAddress("/magda/transport/seek/beats").has_value());
    REQUIRE_FALSE(parseOscAddress("/magda/transport/position/bars").has_value());
}
