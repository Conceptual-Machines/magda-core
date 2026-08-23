#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <string>
#include <vector>

#include "MockMagdaApi.hpp"
#include "magda/daw/api/osc_feedback_live.hpp"
#include "magda/daw/audio/osc/OscAddress.hpp"
#include "magda/daw/audio/osc/OscFeedback.hpp"
#include "magda/daw/audio/osc/OscRouter.hpp"
#include "magda/daw/core/ParameterInfo.hpp"
#include "magda/daw/core/ParameterUtils.hpp"
#include "magda/daw/core/controllers/BindingRegistry.hpp"
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

/// Hands out one capturing sink per surface and keeps them all, which is what
/// makes the per-peer rules observable: a value suppressed to one surface has to
/// be visible arriving at the other.
///
/// The sinks are owned *here* and the projector is handed a forwarder, because
/// the projector destroys a surface's sink when its peer goes away -- and the
/// case worth testing is exactly "and then nothing more was sent to it", which
/// cannot be asserted through an object that has just been destroyed. A sink
/// kept across a peer disappearing and coming back is also the right shape: it
/// is the same surface, and the test can see everything it was ever told.
struct SinkFleet {
    magda::OscFeedbackProjector::SinkFactory factory() {
        return [this](const juce::String& host, int) -> std::unique_ptr<OscMessageSink> {
            auto& captured = owned_[host];
            if (captured == nullptr)
                captured = std::make_unique<CapturingSink>();
            return std::make_unique<Forwarder>(*captured);
        };
    }

    CapturingSink* operator[](const juce::String& host) const {
        const auto found = owned_.find(host);
        return found == owned_.end() ? nullptr : found->second.get();
    }

  private:
    /// What the projector owns and destroys. Holds a reference to the capturing
    /// sink rather than being one.
    class Forwarder : public OscMessageSink {
      public:
        explicit Forwarder(CapturingSink& target) : target_(target) {}

        bool send(const juce::String& address, float value) override {
            return target_.send(address, value);
        }

      private:
        CapturingSink& target_;
    };

    std::map<juce::String, std::unique_ptr<CapturingSink>> owned_;
};

/// One surface, on loopback. Interned directly rather than through a socket:
/// the peer table is what the projector reads, and how a datagram got into it is
/// `OscService`'s business, tested where that is.
constexpr const char* kSurfaceHost = "127.0.0.1";

struct ProjectorRig {
    ProjectorRig() {
        router = std::make_unique<OscRouter>(std::make_unique<DiscardingCommandSink>());
        // `admit` is what the receive loop calls once the router has accepted
        // something. Only answerable peers are surfaces, so interning alone
        // leaves nothing to project onto.
        peer = router->peers().admit(kSurfaceHost, 1000);
        projector = std::make_unique<magda::OscFeedbackProjector>(api, changes, *router, nullptr,
                                                                  fleet.factory());
    }

    /// The surface is built by the first tick, so this is only valid after one —
    /// which every case below does before it looks at what was sent.
    CapturingSink& sink() const {
        auto* found = fleet[kSurfaceHost];
        REQUIRE(found != nullptr);
        return *found;
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
    SinkFleet fleet;
    magda::osc::OscPeerId peer = magda::osc::kNoOscPeer;
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

    REQUIRE(rig.sink().find("/magda/track/1/volume") != nullptr);
    REQUIRE(rig.sink().find("/magda/track/1/volume")->value ==
            Approx(expectedFaderPosition(0.5f)).margin(1.0e-5));
    REQUIRE(rig.sink().find("/magda/track/1/pan")->value == Approx(0.0f).margin(1.0e-5));
    REQUIRE(rig.sink().find("/magda/track/1/mute")->value == Approx(1.0f));
    REQUIRE(rig.sink().find("/magda/track/1/solo")->value == Approx(0.0f));
    REQUIRE(rig.sink().find("/magda/track/2/volume") != nullptr);

    // A position with no track behind it is silent rather than zeroed.
    REQUIRE(rig.sink().find("/magda/track/3/volume") == nullptr);
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

    REQUIRE(rig.sink().find("/magda/track/2/volume")->value ==
            Approx(expectedFaderPosition(0.25f)).margin(1.0e-5));
}

TEST_CASE("A shrinking project retires the strips it no longer has", "[osc][feedback]") {
    ProjectorRig rig;
    rig.addTrack("One");
    rig.addTrack("Two");
    rig.markTracksChanged();
    rig.projector->tick();
    REQUIRE(rig.sink().find("/magda/track/2/volume") != nullptr);

    rig.api.tracks_.tracks.pop_back();
    rig.sink().clear();
    rig.projector->requestSnapshot();
    rig.projector->tick();

    // Strip 1 is resent by the snapshot; strip 2 no longer exists and is not.
    REQUIRE(rig.sink().find("/magda/track/1/volume") != nullptr);
    REQUIRE(rig.sink().find("/magda/track/2/volume") == nullptr);
}

TEST_CASE("Play and stop stay opposite", "[osc][feedback]") {
    ProjectorRig rig;

    rig.projector->tick();
    REQUIRE(rig.sink().find("/magda/transport/play")->value == Approx(0.0f));
    REQUIRE(rig.sink().find("/magda/transport/stop")->value == Approx(1.0f));

    rig.sink().clear();
    rig.api.transport_.playing = true;
    rig.projector->tick();
    REQUIRE(rig.sink().find("/magda/transport/play")->value == Approx(1.0f));
    REQUIRE(rig.sink().find("/magda/transport/stop")->value == Approx(0.0f));
}

TEST_CASE("Transport values that did not move are not resent", "[osc][feedback]") {
    ProjectorRig rig;
    rig.projector->tick();
    rig.sink().clear();

    rig.projector->tick();
    REQUIRE(rig.sink().messages.empty());

    rig.api.transport_.positionBeats = 8.0;
    rig.projector->tick();
    REQUIRE(rig.sink().countFor("/magda/transport/position") == 1);
    REQUIRE(rig.sink().find("/magda/transport/position")->value == Approx(8.0f));
}

TEST_CASE("The focused device's macros go out, and stop when focus goes", "[osc][feedback]") {
    ProjectorRig rig;
    rig.api.focused_.focused = true;
    rig.api.focused_.macroValues = {0.25f, 0.75f};

    rig.projector->tick();
    REQUIRE(rig.sink().find("/magda/focused/macro/1")->value == Approx(0.25f));
    REQUIRE(rig.sink().find("/magda/focused/macro/2")->value == Approx(0.75f));

    rig.api.focused_.focused = false;
    rig.sink().clear();
    rig.projector->requestSnapshot();
    rig.projector->tick();
    REQUIRE(rig.sink().find("/magda/focused/macro/1") == nullptr);
}

// ============================================================================
// A surface is a peer, and everything per-peer follows from that
// ============================================================================

TEST_CASE("A surface that appears is snapshotted, and the one already there is not",
          "[osc][feedback]") {
    // What #2091 could only approximate by watching for inbound traffic after a
    // silence. A peer arriving *is* the event, so the surface that has just
    // joined is caught up and the one mid-gesture is not interrupted.
    ProjectorRig rig;
    rig.addTrack("Drums");
    rig.markTracksChanged();
    rig.projector->tick();
    rig.sink().clear();

    // Nothing has moved, so an ordinary tick says nothing to anybody.
    rig.projector->tick();
    REQUIRE(rig.sink().messages.empty());

    // Well beyond the silence threshold for the first surface. A generation
    // change caused by this new peer must not look like the first peer resumed.
    const auto second = rig.router->peers().admit("10.0.0.5", 7000);
    REQUIRE(second != rig.peer);
    rig.projector->tick();

    auto* joined = rig.fleet["10.0.0.5"];
    REQUIRE(joined != nullptr);
    REQUIRE(joined->find("/magda/track/1/volume") != nullptr);
    REQUIRE(joined->find("/magda/transport/play") != nullptr);

    REQUIRE(rig.sink().messages.empty());
}

TEST_CASE("A surface returning after five seconds receives a fresh snapshot", "[osc][feedback]") {
    ProjectorRig rig;
    rig.addTrack("Drums");
    rig.markTracksChanged();
    rig.projector->tick();
    rig.sink().clear();

    const auto settled = rig.router->peers().generation();
    const auto arrival = rig.router->peers().intern(kSurfaceHost, 7000);
    REQUIRE(arrival.id == rig.peer);
    REQUIRE(arrival.answerable);
    REQUIRE(rig.router->peers().generation() == settled + 1);

    rig.projector->tick();
    REQUIRE(rig.sink().find("/magda/track/1/volume") != nullptr);
    REQUIRE(rig.sink().find("/magda/transport/play") != nullptr);
}

TEST_CASE("A value from one surface is suppressed to it and sent to the other", "[osc][feedback]") {
    // The case one shared table cannot express: with a single destination, a
    // fader moved on A is read as an echo for everyone, so B sits showing a
    // value nobody holds.
    ProjectorRig rig;
    const auto first = rig.addTrack("Drums");
    rig.router->peers().admit("10.0.0.5", 2000);
    rig.markTracksChanged();
    rig.projector->tick();

    auto* other = rig.fleet["10.0.0.5"];
    REQUIRE(other != nullptr);
    rig.sink().clear();
    other->clear();

    // A fader move that arrived from the first surface, and the model state it
    // produced.
    const float position = expectedFaderPosition(0.5f);
    rig.router->handleMessage(juce::OSCMessage("/magda/track/1/volume", position), rig.peer);
    rig.track(first)->volume = 0.5f;

    rig.markTracksChanged();
    rig.projector->tick();

    REQUIRE(rig.sink().find("/magda/track/1/volume") == nullptr);
    REQUIRE(other->find("/magda/track/1/volume") != nullptr);
    REQUIRE(other->find("/magda/track/1/volume")->value == Approx(position).margin(1.0e-5));
}

TEST_CASE("A surface stops being answered when its peer goes", "[osc][feedback]") {
    ProjectorRig rig;
    rig.addTrack("Drums");
    rig.markTracksChanged();
    rig.projector->tick();
    REQUIRE(rig.projector->surfaceCount() == 1);

    // What a rebind does: the peers behind a socket that has closed are not
    // peers of the one that replaces it.
    rig.router->peers().clear();
    rig.sink().clear();
    rig.projector->tick();

    REQUIRE(rig.projector->surfaceCount() == 0);
    REQUIRE(rig.sink().messages.empty());
}

TEST_CASE("A peer that has said nothing usable is never answered", "[osc][feedback]") {
    // The reflector, closed. Four bytes of anything with a spoofed source
    // address would otherwise mint a peer, and the next tick would open a sender
    // to that host and stream a whole project at it.
    ProjectorRig rig;
    rig.addTrack("Drums");
    rig.markTracksChanged();
    rig.projector->tick();
    REQUIRE(rig.projector->surfaceCount() == 1);

    rig.router->peers().intern("203.0.113.9", 2000);  // heard from, never understood
    rig.projector->tick();

    REQUIRE(rig.projector->surfaceCount() == 1);
    REQUIRE(rig.fleet["203.0.113.9"] == nullptr);

    // And it becomes one the moment it says something MAGDA understands.
    rig.router->peers().admit("203.0.113.9", 2001);
    rig.projector->tick();
    REQUIRE(rig.projector->surfaceCount() == 2);
    REQUIRE(rig.fleet["203.0.113.9"] != nullptr);
}

TEST_CASE("A full table of surfaces cannot be churned by unvalidated traffic", "[osc][feedback]") {
    // The hole the first pass left: preferring to evict a peer that has said
    // nothing usable only helps once such a peer exists. With every slot holding
    // a real surface, one spoofed junk packet would otherwise take the quietest
    // one, drop its surface, and re-snapshot it the moment it spoke again --
    // repeatable for as long as the flood lasts.
    ProjectorRig rig;  // holds one surface already
    for (int i = 2; i <= OscPeers::kMaxPeers; ++i)
        rig.router->peers().admit("10.0.0." + juce::String(i), 1000 + i);
    REQUIRE(rig.router->peers().count() == OscPeers::kMaxPeers);

    rig.projector->tick();
    REQUIRE(rig.projector->surfaceCount() == OscPeers::kMaxPeers);
    const auto settled = rig.router->peers().generation();

    // A host nobody has heard anything usable from, arriving at capacity.
    for (int packet = 0; packet < 20; ++packet)
        REQUIRE(rig.router->peers().intern("203.0.113.9", 5000 + packet).id == kNoOscPeer);

    REQUIRE(rig.router->peers().count() == OscPeers::kMaxPeers);
    REQUIRE(rig.router->peers().generation() == settled);
    REQUIRE(rig.router->peers().hostFor(rig.peer) == kSurfaceHost);

    rig.projector->tick();
    REQUIRE(rig.projector->surfaceCount() == OscPeers::kMaxPeers);
}

TEST_CASE("A ninth surface that proves itself does take the quietest slot", "[osc][feedback]") {
    // The other half of the same rule: refusing an unvalidated host must not
    // become refusing a real one, or the table would freeze on whatever eight
    // surfaces happened to connect first.
    ProjectorRig rig;
    for (int i = 2; i <= OscPeers::kMaxPeers; ++i)
        rig.router->peers().admit("10.0.0." + juce::String(i), 1000 + i);

    // The rig's own peer was admitted at 1000, so it is the quietest and it is
    // the one displaced.
    const auto ninth = rig.router->peers().admit("10.0.0.99", 9000);
    REQUIRE(rig.router->peers().count() == OscPeers::kMaxPeers);
    REQUIRE(rig.router->peers().hostFor(ninth) == "10.0.0.99");

    // Its slot was reused; its id was not. An id names a host for the life of
    // the process, so the number the displaced peer had now names nobody.
    REQUIRE(ninth != rig.peer);
    REQUIRE(rig.router->peers().hostFor(rig.peer).isEmpty());
}

TEST_CASE("Unvalidated traffic never consumes a peer id", "[osc][feedback]") {
    // Ids have to be unique for the life of the process, so what can mint one is
    // what bounds the counter. Only `admit` does, which means a flood of spoofed
    // hosts cannot advance it at all however long it runs.
    OscPeers peers;
    const auto first = peers.admit("10.0.0.1", 1000);
    REQUIRE(first != kNoOscPeer);

    // Far more distinct hosts than the table holds, none of them understood.
    for (int i = 0; i < 500; ++i)
        REQUIRE(peers.intern("203.0.113." + juce::String(i % 250), 2000 + i).id == kNoOscPeer);

    const auto second = peers.admit("10.0.0.2", 3000);
    REQUIRE(second == first + 1);
    REQUIRE(peers.hostFor(first) == "10.0.0.1");  // and the real one is still there
}

TEST_CASE("A sink that fails to open is tried again on the next tick", "[osc][feedback]") {
    // The peer set has not changed, so the generation fast path would otherwise
    // never look at this peer again: one transient socket failure would disable
    // feedback for that surface for the rest of the session.
    magda::test::MockMagdaApi api;
    magda::remote::ChangeSource changes;
    OscRouter router{std::make_unique<DiscardingCommandSink>()};
    router.peers().admit(kSurfaceHost, 1000);

    bool refuse = true;
    CapturingSink* built = nullptr;
    magda::OscFeedbackProjector projector{
        api, changes, router, nullptr,
        [&refuse, &built](const juce::String&, int) -> std::unique_ptr<OscMessageSink> {
            if (refuse)
                return nullptr;
            auto owned = std::make_unique<CapturingSink>();
            built = owned.get();
            return owned;
        }};

    projector.tick();
    REQUIRE(projector.surfaceCount() == 0);

    refuse = false;
    projector.tick();
    REQUIRE(projector.surfaceCount() == 1);
    REQUIRE(built != nullptr);
}

TEST_CASE("A gesture that lands before the first tick still counts as an echo", "[osc][feedback]") {
    // The drain that carries a first message runs before the tick that would
    // have built the surface, so the tap has to sync rather than drop the bit.
    ProjectorRig rig;
    const auto first = rig.addTrack("Drums");

    const float position = expectedFaderPosition(0.5f);
    rig.router->handleMessage(juce::OSCMessage("/magda/track/1/volume", position), rig.peer);
    rig.track(first)->volume = 0.5f;
    rig.markTracksChanged();
    rig.projector->tick();

    // The first tick is a snapshot, which is not suppressed by an echo — that is
    // the point of a snapshot. What the bit has to survive is the tick after it.
    rig.sink().clear();
    rig.markTracksChanged();
    rig.projector->tick();
    REQUIRE(rig.sink().find("/magda/track/1/volume") == nullptr);
}

namespace {

/// Stands in for the engine behind a bound target. The reader's own inversions
/// are covered where the reader is; what these cases are about is *when* the
/// projector goes looking.
class StubParamReader : public magda::ControllerParamReader {
  public:
    std::optional<float> read(const magda::ResolveResult&) override {
        return value;
    }

    float value = 0.0f;
};

/// A registry the test owns for the length of one case. The registry is a
/// singleton shared with every other test in the binary, so what a case adds it
/// takes away again.
struct ScopedGlobalBinding {
    explicit ScopedGlobalBinding(const juce::String& address) {
        binding.id = juce::Uuid();
        binding.source.kind = magda::BindingSourceKind::Osc;
        binding.source.oscAddress = address;
        binding.target = magda::ControlTarget::trackVolume(1);
        magda::BindingRegistry::getInstance().add(magda::BindingScope::Global, binding);
    }

    ~ScopedGlobalBinding() {
        magda::BindingRegistry::getInstance().remove(magda::BindingScope::Global, binding.id);
    }

    magda::Binding binding;
};

}  // namespace

TEST_CASE("A binding added after the last tick is projected on the next one", "[osc][feedback]") {
    // The gap this closes: OscService listens to the registry so the input goes
    // live, and without the same listener here the address it comes from stays
    // blank until an unrelated edit happens to dirty the pass. OSC learn is the
    // sharpest case, since its capture is consumed rather than applied.
    auto owned = std::make_unique<StubParamReader>();
    auto* reader = owned.get();

    SinkFleet fleet;
    magda::test::MockMagdaApi api;
    magda::remote::ChangeSource changes;
    OscRouter router{std::make_unique<DiscardingCommandSink>()};
    router.peers().admit(kSurfaceHost, 1000);
    magda::OscFeedbackProjector projector{api, changes, router, std::move(owned), fleet.factory()};

    reader->value = 0.4f;
    projector.tick();
    auto* sink = fleet[kSurfaceHost];
    REQUIRE(sink != nullptr);
    sink->clear();

    // Nothing is dirty, so an ordinary tick says nothing.
    projector.tick();
    REQUIRE(sink->find("/x/fader") == nullptr);

    ScopedGlobalBinding bound{"/x/fader"};
    projector.tick();

    const auto* message = sink->find("/x/fader");
    REQUIRE(message != nullptr);
    REQUIRE(message->value == Approx(0.4f));

    // And it holds still once the surface has been told.
    sink->clear();
    projector.tick();
    REQUIRE(sink->find("/x/fader") == nullptr);

    // A move of the target does reach it.
    reader->value = 0.9f;
    changes.markChanged(magda::remote::Topic::Automation, 2);
    changes.flush();
    projector.tick();
    REQUIRE(sink->find("/x/fader") != nullptr);
    REQUIRE(sink->find("/x/fader")->value == Approx(0.9f));
}

TEST_CASE("A value the surface set is not projected back at it", "[osc][feedback]") {
    ProjectorRig rig;
    const auto first = rig.addTrack("Drums");
    rig.markTracksChanged();
    rig.projector->tick();
    rig.sink().clear();

    // A fader move from that surface, applied through the router the way a real
    // one is, and the model state it produced.
    const float position = expectedFaderPosition(0.5f);
    rig.router->handleMessage(juce::OSCMessage("/magda/track/1/volume", position), rig.peer);
    rig.track(first)->volume = 0.5f;

    rig.markTracksChanged();
    rig.projector->tick();
    REQUIRE(rig.sink().find("/magda/track/1/volume") == nullptr);

    // But a move MAGDA makes afterwards does reach the surface.
    rig.sink().clear();
    rig.track(first)->volume = 1.0f;
    rig.markTracksChanged();
    rig.projector->tick();
    REQUIRE(rig.sink().find("/magda/track/1/volume") != nullptr);
}

TEST_CASE("Work queued by a peer that has gone is not the echo of its replacement",
          "[osc][feedback]") {
    // Peer ids used to be slot indices, so a host taking a slot over inherited
    // the number of the one it displaced. A binding value still sitting in the
    // router's queue from the old peer would then be recorded as the new
    // surface's own echo, and `publishBinding` would suppress its first real
    // update while it was still showing the pre-drain snapshot.
    auto owned = std::make_unique<StubParamReader>();
    auto* reader = owned.get();

    SinkFleet fleet;
    magda::test::MockMagdaApi api;
    magda::remote::ChangeSource changes;
    OscRouter router{std::make_unique<DiscardingCommandSink>()};
    // The test decides when the drain runs, which is the whole point: the queued
    // value has to still be in flight when the peer behind it goes away.
    router.setDrainScheduler([]() {});
    const auto departing = router.peers().admit(kSurfaceHost, 1000);

    magda::OscFeedbackProjector projector{api, changes, router, std::move(owned), fleet.factory()};

    ScopedGlobalBinding bound{"/x/fader"};
    // The registry feeds the router through OscService in the app; a bare router
    // has to be told.
    router.updateBindings(
        magda::BindingRegistry::getInstance().bindings(magda::BindingScope::Global));

    reader->value = 0.4f;
    projector.tick();
    REQUIRE(fleet[kSurfaceHost] != nullptr);
    REQUIRE(fleet[kSurfaceHost]->find("/x/fader") != nullptr);

    // A move on the departing surface, queued and not yet applied.
    REQUIRE(router.handleMessage(juce::OSCMessage("/x/fader", 0.9f), departing));

    // That surface goes, and another takes its slot.
    router.peers().clear();
    const auto arriving = router.peers().admit("10.0.0.42", 2000);
    REQUIRE(arriving != departing);
    projector.tick();

    auto* replacement = fleet["10.0.0.42"];
    REQUIRE(replacement != nullptr);
    REQUIRE(replacement->find("/x/fader") != nullptr);  // snapshotted at 0.4
    replacement->clear();

    // Now the queued value lands. It belongs to a peer that no longer exists, so
    // it must not be recorded against the surface that replaced it.
    router.drainPending();

    reader->value = 0.9f;
    changes.markChanged(magda::remote::Topic::Automation, 2);
    changes.flush();
    projector.tick();

    const auto* update = replacement->find("/x/fader");
    REQUIRE(update != nullptr);
    REQUIRE(update->value == Approx(0.9f));
}
