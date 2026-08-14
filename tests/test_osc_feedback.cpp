#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "MockMagdaApi.hpp"
#include "magda/daw/api/osc_feedback_live.hpp"
#include "magda/daw/audio/osc/OscAddress.hpp"
#include "magda/daw/audio/osc/OscFeedback.hpp"
#include "magda/daw/audio/osc/OscRouter.hpp"
#include "magda/daw/core/ParameterInfo.hpp"
#include "magda/daw/core/ParameterUtils.hpp"
#include "magda/daw/core/controllers/BindingTransform.hpp"

using namespace magda::osc;
using Catch::Approx;

namespace {

struct Message {
    juce::String address;
    float value = 0.0f;
};

/// The seam the whole of `OscFeedback` is testable through: no socket, no
/// destination, and every datagram it would have sent kept in order.
class CapturingSink : public OscMessageSink {
  public:
    bool send(const juce::String& address, float value) override {
        messages.push_back(Message{address, value});
        return connected;
    }

    std::vector<Message> messages;
    bool connected = true;

    void clear() {
        messages.clear();
    }

    int countFor(const char* address) const {
        return static_cast<int>(
            std::count_if(messages.begin(), messages.end(),
                          [address](const Message& m) { return m.address == address; }));
    }

    const Message* find(const char* address) const {
        for (const auto& message : messages)
            if (message.address == address)
                return &message;
        return nullptr;
    }
};

struct Rig {
    CapturingSink* sink = nullptr;
    std::unique_ptr<OscFeedback> feedback;

    Rig() {
        auto owned = std::make_unique<CapturingSink>();
        sink = owned.get();
        feedback = std::make_unique<OscFeedback>(std::move(owned));
    }

    OscFeedback& operator*() const {
        return *feedback;
    }
    OscFeedback* operator->() const {
        return feedback.get();
    }
};

int slotOf(OscCommandKind kind, int index = 0, int subIndex = 0) {
    return oscSlotIndex(OscCommand{kind, index, subIndex});
}

}  // namespace

// ============================================================================
// The formatter is the parser read backwards
// ============================================================================

TEST_CASE("Every slot formats to an address that parses back to it", "[osc][feedback]") {
    for (int slot = 0; slot < kOscSlotCount; ++slot) {
        const auto command = oscCommandForSlot(slot);
        const auto address = formatOscAddress(command);
        INFO("slot " << slot << " -> " << address.toStdString());

        const auto reparsed = parseOscAddress(address);
        REQUIRE(reparsed.has_value());
        REQUIRE(*reparsed == command);
        REQUIRE(oscSlotIndex(*reparsed) == slot);
    }
}

TEST_CASE("Addresses are spelled the way a template writes them", "[osc][feedback]") {
    REQUIRE(formatOscAddress(OscCommand{OscCommandKind::TrackVolume, 3, 0}) ==
            "/magda/track/3/volume");
    REQUIRE(formatOscAddress(OscCommand{OscCommandKind::TrackSend, 2, 4}) ==
            "/magda/track/2/send/4");
    REQUIRE(formatOscAddress(OscCommand{OscCommandKind::FocusedMacro, 7, 0}) ==
            "/magda/focused/macro/7");
    REQUIRE(formatOscAddress(OscCommand{OscCommandKind::TransportSeekBars, 0, 0}) ==
            "/magda/transport/seek/bars");
}

// ============================================================================
// Diffing: an address is sent when it changed, and not otherwise
// ============================================================================

TEST_CASE("A value sends once and an unchanged republish sends nothing", "[osc][feedback]") {
    Rig rig;
    const int slot = slotOf(OscCommandKind::TrackVolume, 3);

    rig->publish(slot, 0.62f);
    REQUIRE(rig->flush() == 1);
    REQUIRE(rig.sink->messages.size() == 1);
    REQUIRE(rig.sink->messages[0].address == "/magda/track/3/volume");
    REQUIRE(rig.sink->messages[0].value == Approx(0.62f));

    rig->publish(slot, 0.62f);
    REQUIRE(rig->flush() == 0);

    rig->publish(slot, 0.70f);
    REQUIRE(rig->flush() == 1);
    REQUIRE(rig.sink->messages.back().value == Approx(0.70f));
}

TEST_CASE("A value that returns to what the surface holds before a flush sends nothing",
          "[osc][feedback]") {
    Rig rig;
    const int slot = slotOf(OscCommandKind::TrackPan, 1);

    rig->publish(slot, 0.5f);
    REQUIRE(rig->flush() == 1);

    // Latest value wins, and the latest value is the one already shown.
    rig->publish(slot, 0.9f);
    rig->publish(slot, 0.5f);
    REQUIRE(rig->flush() == 0);
}

TEST_CASE("A snapshot resends everything that has a value", "[osc][feedback]") {
    Rig rig;
    rig->publish(slotOf(OscCommandKind::TrackVolume, 1), 0.5f);
    rig->publish(slotOf(OscCommandKind::TrackVolume, 2), 0.25f);
    rig->publish(slotOf(OscCommandKind::MasterVolume), 0.8f);
    REQUIRE(rig->flush() == 3);

    rig.sink->clear();
    REQUIRE(rig->flush() == 0);  // nothing changed

    rig->requestSnapshot();
    REQUIRE(rig->flush() == 3);
    REQUIRE(rig.sink->find("/magda/track/1/volume") != nullptr);
    REQUIRE(rig.sink->find("/magda/track/2/volume") != nullptr);
    REQUIRE(rig.sink->find("/magda/master/volume") != nullptr);
}

TEST_CASE("A retired slot stops being part of a snapshot", "[osc][feedback]") {
    Rig rig;
    const int slot = slotOf(OscCommandKind::TrackVolume, 4);

    rig->publish(slot, 0.4f);
    REQUIRE(rig->flush() == 1);

    rig.sink->clear();
    rig->retire(slot);
    rig->requestSnapshot();
    REQUIRE(rig->flush() == 0);
    REQUIRE(rig.sink->messages.empty());
}

// ============================================================================
// Echo suppression, as the two cases that tell it apart from a comparison
// ============================================================================

TEST_CASE("A value that just arrived from the surface is not sent back", "[osc][feedback]") {
    Rig rig;
    const int slot = slotOf(OscCommandKind::TrackVolume, 1);

    rig->noteReceived(slot, 0.62f);
    rig->publish(slot, 0.62f);
    REQUIRE(rig->flush() == 0);
    REQUIRE(rig.sink->messages.empty());
}

TEST_CASE("A suppressed value is still what the next change is measured against",
          "[osc][feedback]") {
    Rig rig;
    const int slot = slotOf(OscCommandKind::TrackVolume, 1);

    rig->noteReceived(slot, 0.62f);
    rig->publish(slot, 0.62f);
    REQUIRE(rig->flush() == 0);

    rig->publish(slot, 0.75f);
    REQUIRE(rig->flush() == 1);
    REQUIRE(rig.sink->messages.back().value == Approx(0.75f));
}

TEST_CASE("Returning to a received value after moving away does send", "[osc][feedback]") {
    // The case a value-only echo rule gets wrong: the surface put the fader at
    // 0.62, MAGDA's own fader went to 0.9, and coming back to 0.62 has to reach
    // the surface or it stays showing 0.9 forever.
    Rig rig;
    const int slot = slotOf(OscCommandKind::TrackVolume, 1);

    rig->noteReceived(slot, 0.62f);
    rig->publish(slot, 0.62f);
    REQUIRE(rig->flush() == 0);

    rig->publish(slot, 0.9f);
    REQUIRE(rig->flush() == 1);

    rig->publish(slot, 0.62f);
    REQUIRE(rig->flush() == 1);
    REQUIRE(rig.sink->messages.back().value == Approx(0.62f));
}

TEST_CASE("A snapshot is not suppressed by an echo", "[osc][feedback]") {
    Rig rig;
    const int slot = slotOf(OscCommandKind::TrackVolume, 1);

    rig->noteReceived(slot, 0.62f);
    rig->publish(slot, 0.62f);
    rig->requestSnapshot();
    REQUIRE(rig->flush() == 1);
}

TEST_CASE("A rounded echo is still an echo", "[osc][feedback]") {
    // A normalized value through a parameter's real range and back does not
    // return the bit pattern it left as.
    Rig rig;
    const int slot = slotOf(OscCommandKind::TrackVolume, 1);

    rig->noteReceived(slot, 0.62f);
    rig->publish(slot, 0.62f + (OscFeedback::kEchoEpsilon / 2.0f));
    REQUIRE(rig->flush() == 0);
}

// ============================================================================
// Deltas have no state to echo
// ============================================================================

TEST_CASE("The seek addresses never send", "[osc][feedback]") {
    Rig rig;
    rig->publish(slotOf(OscCommandKind::TransportSeekBeats), 4.0f);
    rig->publish(slotOf(OscCommandKind::TransportSeekBars), 1.0f);
    rig->requestSnapshot();

    REQUIRE(rig->flush() == 0);
    REQUIRE(rig.sink->messages.empty());
}

TEST_CASE("Play and stop are one state over two addresses", "[osc][feedback]") {
    Rig rig;
    rig->publish(slotOf(OscCommandKind::TransportPlay), 1.0f);
    rig->publish(slotOf(OscCommandKind::TransportStop), 0.0f);
    REQUIRE(rig->flush() == 2);

    REQUIRE(rig.sink->find("/magda/transport/play")->value == Approx(1.0f));
    REQUIRE(rig.sink->find("/magda/transport/stop")->value == Approx(0.0f));
}

// ============================================================================
// The budget defers rather than drops
// ============================================================================

TEST_CASE("A flush over the cap carries the rest to the next one", "[osc][feedback]") {
    Rig rig;

    // More addresses than one flush can carry. Volume and pan for the first
    // tracks rather than volume alone: the cap is above the track count on
    // purpose, so overflowing it takes more than one member per strip.
    const int overflow = 20;
    const int wanted = OscFeedback::kMaxMessagesPerFlush + overflow;

    std::vector<OscCommand> commands;
    for (int track = 1; track <= kMaxTrackNumber && static_cast<int>(commands.size()) < wanted;
         ++track) {
        commands.push_back(OscCommand{OscCommandKind::TrackVolume, track, 0});
        if (static_cast<int>(commands.size()) < wanted)
            commands.push_back(OscCommand{OscCommandKind::TrackPan, track, 0});
    }
    REQUIRE(static_cast<int>(commands.size()) == wanted);

    for (const auto& command : commands)
        rig->publish(command, 0.5f);

    REQUIRE(rig->flush() == OscFeedback::kMaxMessagesPerFlush);
    REQUIRE(rig->deferredMessageCount() == static_cast<std::uint64_t>(overflow));

    REQUIRE(rig->flush() == overflow);
    REQUIRE(rig->flush() == 0);

    // Every address arrived exactly once across the two flushes: deferred, not
    // dropped, and not duplicated either.
    REQUIRE(static_cast<int>(rig.sink->messages.size()) == wanted);
    for (const auto& command : commands) {
        const auto address = formatOscAddress(command);
        INFO(address.toStdString());
        REQUIRE(rig.sink->countFor(address.toRawUTF8()) == 1);
    }
}

TEST_CASE("A send that fails leaves the value owed", "[osc][feedback]") {
    Rig rig;
    rig.sink->connected = false;

    const int slot = slotOf(OscCommandKind::TrackVolume, 1);
    rig->publish(slot, 0.5f);
    REQUIRE(rig->flush() == 0);

    rig.sink->connected = true;
    rig.sink->clear();
    REQUIRE(rig->flush() == 1);
    REQUIRE(rig.sink->messages[0].value == Approx(0.5f));
}

TEST_CASE("Reset forgets what the surface was told", "[osc][feedback]") {
    Rig rig;
    const int slot = slotOf(OscCommandKind::TrackVolume, 1);

    rig->publish(slot, 0.5f);
    REQUIRE(rig->flush() == 1);

    rig->reset();
    rig.sink->clear();
    rig->publish(slot, 0.5f);
    REQUIRE(rig->flush() == 1);  // a new surface has been told nothing
}

// ============================================================================
// A bound address needs the way back through the binding's own shape
// ============================================================================

TEST_CASE("Every curve inverts", "[osc][feedback]") {
    using magda::BindingCurve;

    for (auto curve :
         {BindingCurve::Linear, BindingCurve::Log, BindingCurve::Exp, BindingCurve::SCurve}) {
        for (int step = 0; step <= 20; ++step) {
            const float x = static_cast<float>(step) / 20.0f;
            const float round = magda::invertCurve(curve, magda::applyCurve(curve, x));
            INFO("curve " << static_cast<int>(curve) << " at " << x);
            REQUIRE(round == Approx(x).margin(1.0e-4));
        }
    }
}

TEST_CASE("A range inverts, and a degenerate one answers the bottom", "[osc][feedback]") {
    magda::BindingRange range;
    range.min = 0.2f;
    range.max = 0.8f;

    for (int step = 0; step <= 10; ++step) {
        const float x = static_cast<float>(step) / 10.0f;
        REQUIRE(magda::invertRange(range, magda::applyRange(range, x)) == Approx(x).margin(1.0e-5));
    }

    magda::BindingRange degenerate;
    degenerate.min = 0.5f;
    degenerate.max = 0.5f;
    REQUIRE(magda::invertRange(degenerate, 0.5f) == Approx(0.0f));
}

// ============================================================================
// The projection: what the model says, on the addresses that say it
// ============================================================================

namespace {

/// The router needs somewhere to apply what arrives. Nothing here reads it —
/// these tests are about the answering direction.
class DiscardingCommandSink : public OscCommandSink {
  public:
    void apply(const OscCommand&, float) override {}
};

struct ProjectorRig {
    ProjectorRig() {
        auto owned = std::make_unique<CapturingSink>();
        sink = owned.get();
        router = std::make_unique<OscRouter>(std::make_unique<DiscardingCommandSink>());
        projector = std::make_unique<magda::OscFeedbackProjector>(api, changes, *router, nullptr,
                                                                  std::move(owned));
    }

    /// A track at the end of the mixer, visible, at unity gain and centre.
    magda::TrackId addTrack(const juce::String& name) {
        magda::TrackInfo track;
        track.id = nextId++;
        track.name = name;
        track.viewSettings.setVisible(magda::ViewMode::Mix, true);
        api.tracks_.tracks.push_back(track);
        return track.id;
    }

    magda::TrackInfo* track(magda::TrackId id) {
        for (auto& candidate : api.tracks_.tracks)
            if (candidate.id == id)
                return &candidate;
        return nullptr;
    }

    /// What `ModelChangeBridge` does when a track property moves. The mock model
    /// notifies nobody, so a test that edits it says so through the same source
    /// the projector listens to — `flush` delivers on this thread, which is the
    /// headless path `ChangeSource` documents.
    void markTracksChanged() {
        changes.markChanged(magda::remote::Topic::Tracks, ++revision);
        changes.flush();
    }

    magda::test::MockMagdaApi api;
    magda::remote::ChangeSource changes;
    CapturingSink* sink = nullptr;
    std::unique_ptr<OscRouter> router;
    std::unique_ptr<magda::OscFeedbackProjector> projector;
    magda::TrackId nextId = 1;
    magda::remote::Revision revision = magda::remote::INITIAL_REVISION;
};

/// The normalized fader position a linear gain corresponds to — the same
/// conversion the writer inverts, restated here so the test does not simply
/// repeat the projector's arithmetic back at itself.
float expectedFaderPosition(float gain) {
    const auto info = magda::ParameterPresets::faderVolume(-1, "Volume");
    const float dB = 20.0f * std::log10(gain);
    return magda::ParameterUtils::realToNormalized(dB, info);
}

}  // namespace

TEST_CASE("A mixer strip's members reach the addresses that name them", "[osc][feedback]") {
    ProjectorRig rig;
    const auto first = rig.addTrack("Drums");
    rig.addTrack("Bass");

    rig.track(first)->volume = 0.5f;  // -6 dB
    rig.track(first)->pan = -1.0f;
    rig.track(first)->muted = true;

    rig.markTracksChanged();
    rig.projector->tick();

    REQUIRE(rig.sink->find("/magda/track/1/volume") != nullptr);
    REQUIRE(rig.sink->find("/magda/track/1/volume")->value ==
            Approx(expectedFaderPosition(0.5f)).margin(1.0e-5));
    REQUIRE(rig.sink->find("/magda/track/1/pan")->value == Approx(0.0f).margin(1.0e-5));
    REQUIRE(rig.sink->find("/magda/track/1/mute")->value == Approx(1.0f));
    REQUIRE(rig.sink->find("/magda/track/1/solo")->value == Approx(0.0f));
    REQUIRE(rig.sink->find("/magda/track/2/volume") != nullptr);

    // A position with no track behind it is silent rather than zeroed.
    REQUIRE(rig.sink->find("/magda/track/3/volume") == nullptr);
}

TEST_CASE("A strip's position is the mixer's, not the track's id", "[osc][feedback]") {
    ProjectorRig rig;
    rig.addTrack("One");
    const auto second = rig.addTrack("Two");
    rig.addTrack("Three");

    // Delete the middle track: the third becomes strip 2, exactly as it does on
    // screen. A template addressing eight faders needs eight dense numbers.
    auto& tracks = rig.api.tracks_.tracks;
    tracks.erase(std::remove_if(tracks.begin(), tracks.end(),
                                [second](const magda::TrackInfo& t) { return t.id == second; }),
                 tracks.end());
    rig.track(3)->volume = 0.25f;

    rig.markTracksChanged();
    rig.projector->tick();

    REQUIRE(rig.sink->find("/magda/track/2/volume")->value ==
            Approx(expectedFaderPosition(0.25f)).margin(1.0e-5));
}

TEST_CASE("A shrinking project retires the strips it no longer has", "[osc][feedback]") {
    ProjectorRig rig;
    rig.addTrack("One");
    rig.addTrack("Two");
    rig.markTracksChanged();
    rig.projector->tick();
    REQUIRE(rig.sink->find("/magda/track/2/volume") != nullptr);

    rig.api.tracks_.tracks.pop_back();
    rig.sink->clear();
    rig.projector->requestSnapshot();
    rig.projector->tick();

    // Strip 1 is resent by the snapshot; strip 2 no longer exists and is not.
    REQUIRE(rig.sink->find("/magda/track/1/volume") != nullptr);
    REQUIRE(rig.sink->find("/magda/track/2/volume") == nullptr);
}

TEST_CASE("Play and stop stay opposite", "[osc][feedback]") {
    ProjectorRig rig;

    rig.projector->tick();
    REQUIRE(rig.sink->find("/magda/transport/play")->value == Approx(0.0f));
    REQUIRE(rig.sink->find("/magda/transport/stop")->value == Approx(1.0f));

    rig.sink->clear();
    rig.api.transport_.playing = true;
    rig.projector->tick();
    REQUIRE(rig.sink->find("/magda/transport/play")->value == Approx(1.0f));
    REQUIRE(rig.sink->find("/magda/transport/stop")->value == Approx(0.0f));
}

TEST_CASE("Transport values that did not move are not resent", "[osc][feedback]") {
    ProjectorRig rig;
    rig.projector->tick();
    rig.sink->clear();

    rig.projector->tick();
    REQUIRE(rig.sink->messages.empty());

    rig.api.transport_.positionBeats = 8.0;
    rig.projector->tick();
    REQUIRE(rig.sink->countFor("/magda/transport/position") == 1);
    REQUIRE(rig.sink->find("/magda/transport/position")->value == Approx(8.0f));
}

TEST_CASE("The focused device's macros go out, and stop when focus goes", "[osc][feedback]") {
    ProjectorRig rig;
    rig.api.focused_.focused = true;
    rig.api.focused_.macroValues = {0.25f, 0.75f};

    rig.projector->tick();
    REQUIRE(rig.sink->find("/magda/focused/macro/1")->value == Approx(0.25f));
    REQUIRE(rig.sink->find("/magda/focused/macro/2")->value == Approx(0.75f));

    rig.api.focused_.focused = false;
    rig.sink->clear();
    rig.projector->requestSnapshot();
    rig.projector->tick();
    REQUIRE(rig.sink->find("/magda/focused/macro/1") == nullptr);
}

TEST_CASE("A value the surface set is not projected back at it", "[osc][feedback]") {
    ProjectorRig rig;
    const auto first = rig.addTrack("Drums");
    rig.markTracksChanged();
    rig.projector->tick();
    rig.sink->clear();

    // What the router's drain reports after applying a fader move, and the model
    // state that move produced.
    const float position = expectedFaderPosition(0.5f);
    rig.projector->feedback().noteReceived(
        oscSlotIndex(OscCommand{OscCommandKind::TrackVolume, 1, 0}), position);
    rig.track(first)->volume = 0.5f;

    rig.markTracksChanged();
    rig.projector->tick();
    REQUIRE(rig.sink->find("/magda/track/1/volume") == nullptr);

    // But a move MAGDA makes afterwards does reach the surface.
    rig.sink->clear();
    rig.track(first)->volume = 1.0f;
    rig.markTracksChanged();
    rig.projector->tick();
    REQUIRE(rig.sink->find("/magda/track/1/volume") != nullptr);
}
