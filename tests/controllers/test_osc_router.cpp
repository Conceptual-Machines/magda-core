#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <memory>
#include <vector>

#include "../../magda/daw/audio/osc/OscRouter.hpp"

using namespace magda::osc;
using Catch::Approx;

namespace {

struct Applied {
    OscCommand command;
    float value = 0.0f;
};

class RecordingSink : public OscCommandSink {
  public:
    void apply(const OscCommand& command, float value) override {
        applied.push_back({command, value});
    }

    std::vector<Applied> applied;
};

/**
 * A router whose drains are held rather than run, so a test can see what the
 * coalescing table kept. `drain()` releases them one batch at a time.
 */
struct HeldRouter {
    HeldRouter() {
        auto owned = std::make_unique<RecordingSink>();
        sink = owned.get();
        router = std::make_unique<OscRouter>(std::move(owned));
        router->setDrainScheduler([this]() { ++drainsRequested; });
    }

    bool send(const juce::OSCMessage& message) {
        return router->handleMessage(message);
    }

    void drain() {
        router->drainPending();
    }

    const std::vector<Applied>& applied() const {
        return sink->applied;
    }

    /// Look a value up by what it addressed. A drain walks the slot table, so
    /// what came out is a set rather than a sequence — asserting on position
    /// would be asserting on the slot layout.
    float valueFor(OscCommandKind kind, int index = 0, int subIndex = 0) const {
        for (const auto& entry : sink->applied)
            if (entry.command == OscCommand{kind, index, subIndex})
                return entry.value;
        FAIL("nothing was applied for that command");
        return 0.0f;
    }

    RecordingSink* sink = nullptr;
    std::unique_ptr<OscRouter> router;
    int drainsRequested = 0;
};

}  // namespace

// ============================================================================
// Accepting and applying
// ============================================================================

TEST_CASE("A fader message reaches the sink", "[osc][router]") {
    HeldRouter r;
    REQUIRE(r.router->handleMessage(juce::OSCMessage("/magda/track/3/volume", 0.62f)));
    r.drain();

    REQUIRE(r.applied().size() == 1);
    REQUIRE(r.applied()[0].command.kind == OscCommandKind::TrackVolume);
    REQUIRE(r.applied()[0].command.index == 3);
    REQUIRE(r.applied()[0].value == Approx(0.62f));
}

TEST_CASE("Unknown addresses are declined and change nothing", "[osc][router]") {
    HeldRouter r;
    // False rather than an error: a binding may still want this address.
    REQUIRE_FALSE(r.router->handleMessage(juce::OSCMessage("/magda/track/3/gain", 0.5f)));
    REQUIRE_FALSE(r.router->handleMessage(juce::OSCMessage("/other/thing", 1.0f)));
    r.drain();

    REQUIRE(r.applied().empty());
    REQUIRE(r.router->acceptedMessageCount() == 0);
}

TEST_CASE("Int and float arguments are read the same way", "[osc][router]") {
    // Surfaces disagree about which type a button's 1 is.
    HeldRouter r;
    r.send(juce::OSCMessage("/magda/track/1/mute", 1));
    r.send(juce::OSCMessage("/magda/track/2/mute", 1.0f));
    r.drain();

    REQUIRE(r.applied().size() == 2);
    REQUIRE(r.valueFor(OscCommandKind::TrackMute, 1) == Approx(1.0f));
    REQUIRE(r.valueFor(OscCommandKind::TrackMute, 2) == Approx(1.0f));
}

TEST_CASE("Normalized values are clamped, tempo and position are not", "[osc][router]") {
    HeldRouter r;
    r.send(juce::OSCMessage("/magda/track/1/volume", 1.7f));
    r.send(juce::OSCMessage("/magda/track/2/volume", -0.3f));
    r.send(juce::OSCMessage("/magda/transport/tempo", 174.0f));
    r.send(juce::OSCMessage("/magda/transport/position", 64.5f));
    r.drain();

    REQUIRE(r.applied().size() == 4);
    REQUIRE(r.valueFor(OscCommandKind::TrackVolume, 1) == Approx(1.0f));
    REQUIRE(r.valueFor(OscCommandKind::TrackVolume, 2) == Approx(0.0f));
    // Ranges belong to the model, which knows the project's real limits.
    REQUIRE(r.valueFor(OscCommandKind::TransportTempo) == Approx(174.0f));
    REQUIRE(r.valueFor(OscCommandKind::TransportPosition) == Approx(64.5f));
}

TEST_CASE("A value address with no number says nothing", "[osc][router]") {
    HeldRouter r;
    REQUIRE_FALSE(r.router->handleMessage(juce::OSCMessage("/magda/track/1/volume")));
    // A string where a level belongs is not a level.
    REQUIRE_FALSE(
        r.router->handleMessage(juce::OSCMessage("/magda/track/1/volume", juce::String("up"))));
    r.drain();

    REQUIRE(r.applied().empty());
}

// ============================================================================
// Momentary buttons vs toggles
// ============================================================================

TEST_CASE("Releasing a momentary Play button does not act", "[osc][router]") {
    HeldRouter r;
    // TouchOSC sends 1 on press and 0 on release down the same address.
    REQUIRE(r.router->handleMessage(juce::OSCMessage("/magda/transport/play", 1.0f)));
    REQUIRE_FALSE(r.router->handleMessage(juce::OSCMessage("/magda/transport/play", 0.0f)));
    r.drain();

    REQUIRE(r.applied().size() == 1);
    REQUIRE(r.applied()[0].command.kind == OscCommandKind::TransportPlay);
    REQUIRE(r.applied()[0].value == Approx(1.0f));
}

TEST_CASE("A bare trigger fires", "[osc][router]") {
    HeldRouter r;
    REQUIRE(r.router->handleMessage(juce::OSCMessage("/magda/transport/stop")));
    r.drain();

    REQUIRE(r.applied().size() == 1);
    REQUIRE(r.applied()[0].command.kind == OscCommandKind::TransportStop);
}

TEST_CASE("Toggles set from a value and flip without one", "[osc][router]") {
    HeldRouter r;
    r.send(juce::OSCMessage("/magda/track/1/mute", 1.0f));
    r.send(juce::OSCMessage("/magda/track/2/mute", 0.0f));
    r.send(juce::OSCMessage("/magda/track/3/mute"));
    r.drain();

    REQUIRE(r.applied().size() == 3);
    REQUIRE(r.valueFor(OscCommandKind::TrackMute, 1) == Approx(1.0f));
    // A toggle's 0 is a state, not a button release, so it must survive.
    REQUIRE(r.valueFor(OscCommandKind::TrackMute, 2) == Approx(0.0f));
    REQUIRE(r.valueFor(OscCommandKind::TrackMute, 3) == Approx(kOscToggleRequest));
}

// ============================================================================
// Hostile and malformed arguments
// ============================================================================

TEST_CASE("Non-finite arguments never reach the model", "[osc][router]") {
    // A float32 argument is free to be NaN or an infinity, and jlimit
    // propagates NaN rather than clamping it. This is an unauthenticated UDP
    // port, so one stray packet must not put NaN into a fader or the playhead.
    HeldRouter r;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    REQUIRE_FALSE(r.router->handleMessage(juce::OSCMessage("/magda/track/1/volume", nan)));
    REQUIRE_FALSE(r.router->handleMessage(juce::OSCMessage("/magda/track/1/pan", inf)));
    REQUIRE_FALSE(r.router->handleMessage(juce::OSCMessage("/magda/master/volume", -inf)));
    REQUIRE_FALSE(r.router->handleMessage(juce::OSCMessage("/magda/transport/position", nan)));
    REQUIRE_FALSE(r.router->handleMessage(juce::OSCMessage("/magda/transport/tempo", inf)));
    REQUIRE_FALSE(r.router->handleMessage(juce::OSCMessage("/magda/focused/macro/1", nan)));
    r.drain();

    REQUIRE(r.applied().empty());
}

TEST_CASE("An argument we cannot read is not the same as none", "[osc][router]") {
    // For a toggle, the *absence* of an argument is the request — flip. So a
    // message carrying something unreadable must be ignored rather than read as
    // bare, or a surface speaking an unknown dialect flips mutes.
    HeldRouter r;
    REQUIRE_FALSE(
        r.router->handleMessage(juce::OSCMessage("/magda/track/1/mute", juce::String("on"))));
    REQUIRE_FALSE(
        r.router->handleMessage(juce::OSCMessage("/magda/transport/record", juce::String("go"))));
    REQUIRE_FALSE(r.router->handleMessage(
        juce::OSCMessage("/magda/track/1/mute", std::numeric_limits<float>::quiet_NaN())));
    r.drain();

    REQUIRE(r.applied().empty());
}

// ============================================================================
// Edges keep their order
// ============================================================================

TEST_CASE("Two flips of one toggle cancel", "[osc][router]") {
    // A toggle is an edge, not a value. Latest-value-wins would collapse these
    // into one flip and leave the track muted.
    HeldRouter r;
    r.send(juce::OSCMessage("/magda/track/1/mute"));
    r.send(juce::OSCMessage("/magda/track/1/mute"));
    r.drain();

    REQUIRE(r.applied().size() == 2);
    REQUIRE(r.applied()[0].value == Approx(kOscToggleRequest));
    REQUIRE(r.applied()[1].value == Approx(kOscToggleRequest));
}

TEST_CASE("A set followed by a flip keeps both", "[osc][router]") {
    HeldRouter r;
    r.send(juce::OSCMessage("/magda/track/1/mute", 1.0f));
    r.send(juce::OSCMessage("/magda/track/1/mute"));
    r.drain();

    REQUIRE(r.applied().size() == 2);
    REQUIRE(r.applied()[0].value == Approx(1.0f));
    REQUIRE(r.applied()[1].value == Approx(kOscToggleRequest));
}

TEST_CASE("A stop-locate-play cue lands playing at the new position", "[osc][router]") {
    // The show-control shape: one bundle, delivered atomically into one drain.
    // Play and stop are two addresses over one state, so resolving them by slot
    // number rather than arrival would make this land stopped whatever the
    // sender intended.
    HeldRouter r;
    r.send(juce::OSCMessage("/magda/transport/stop"));
    r.send(juce::OSCMessage("/magda/transport/position", 64.0f));
    r.send(juce::OSCMessage("/magda/transport/play"));
    r.drain();

    REQUIRE(r.applied().size() == 3);
    // The locate first, so the transport rolls from where the cue put it...
    REQUIRE(r.applied()[0].command.kind == OscCommandKind::TransportPosition);
    REQUIRE(r.applied()[0].value == Approx(64.0f));
    // ...then the edges, in the order they were sent.
    REQUIRE(r.applied()[1].command.kind == OscCommandKind::TransportStop);
    REQUIRE(r.applied()[2].command.kind == OscCommandKind::TransportPlay);
}

TEST_CASE("Play then stop lands stopped, and stop then play lands playing", "[osc][router]") {
    {
        HeldRouter r;
        r.send(juce::OSCMessage("/magda/transport/play"));
        r.send(juce::OSCMessage("/magda/transport/stop"));
        r.drain();
        REQUIRE(r.applied().size() == 2);
        REQUIRE(r.applied()[1].command.kind == OscCommandKind::TransportStop);
    }
    {
        HeldRouter r;
        r.send(juce::OSCMessage("/magda/transport/stop"));
        r.send(juce::OSCMessage("/magda/transport/play"));
        r.drain();
        REQUIRE(r.applied().size() == 2);
        REQUIRE(r.applied()[1].command.kind == OscCommandKind::TransportPlay);
    }
}

TEST_CASE("A drain yields even while commands keep arriving", "[osc][router]") {
    // The per-drain cap is what stops a sender holding the message thread
    // inside one drain by pushing as fast as it is consumed. A sink that
    // re-sends on every apply is that sender, deterministically: without the
    // cap the loop below never terminates.
    //
    // The cap is also why the follow-up drain must be posted rather than run
    // through the configured scheduler — the default one applies inline when
    // already on the message thread, which would recurse a stack level per
    // batch instead of yielding. That path needs two threads to reach, so it
    // is not reproducible here; what is testable is the bound it protects.
    class ResendingSink : public OscCommandSink {
      public:
        void apply(const OscCommand& command, float value) override {
            ++applied;
            if (router != nullptr)
                router->handleMessage(juce::OSCMessage("/magda/track/1/mute"));
            juce::ignoreUnused(command, value);
        }

        OscRouter* router = nullptr;
        int applied = 0;
    };

    auto owned = std::make_unique<ResendingSink>();
    auto* sink = owned.get();
    OscRouter router(std::move(owned));
    sink->router = &router;
    router.setDrainScheduler([]() {});

    router.handleMessage(juce::OSCMessage("/magda/track/1/mute"));
    router.drainPending();

    // One drain, one batch: it stopped and handed the thread back.
    REQUIRE(sink->applied == static_cast<int>(OscRouter::kOrderedCapacity));

    // And the remainder the sink queued is still there for the next one.
    sink->applied = 0;
    router.drainPending();
    REQUIRE(sink->applied > 0);
}

TEST_CASE("A flood of presses is bounded and counted", "[osc][router]") {
    // Unbounded ordering would be unbounded memory on an unauthenticated port.
    HeldRouter r;
    for (int i = 0; i < 5000; ++i)
        r.send(juce::OSCMessage("/magda/track/1/mute"));

    REQUIRE(r.router->droppedCommandCount() > 0);
    r.drain();
    // What survived is bounded, and every drain makes room for more.
    REQUIRE(r.applied().size() <= 5000);
    REQUIRE_FALSE(r.applied().empty());
}

// ============================================================================
// Coalescing
// ============================================================================

TEST_CASE("A fader stream collapses to its latest value", "[osc][router]") {
    HeldRouter r;
    for (int i = 0; i <= 100; ++i)
        r.send(juce::OSCMessage("/magda/track/1/volume", static_cast<float>(i) / 100.0f));

    // 101 messages, one drain request: the message thread is asked to wake once
    // however fast the surface sends.
    REQUIRE(r.drainsRequested == 1);
    REQUIRE(r.router->acceptedMessageCount() == 101);
    REQUIRE(r.applied().empty());

    r.drain();
    REQUIRE(r.applied().size() == 1);
    REQUIRE(r.applied()[0].value == Approx(1.0f));
}

TEST_CASE("Separate addresses keep separate values", "[osc][router]") {
    HeldRouter r;
    for (int i = 0; i < 50; ++i) {
        r.send(juce::OSCMessage("/magda/track/1/volume", 0.1f * static_cast<float>(i)));
        r.send(juce::OSCMessage("/magda/track/2/volume", 0.01f * static_cast<float>(i)));
        r.send(juce::OSCMessage("/magda/master/volume", 0.5f));
    }
    r.drain();

    REQUIRE(r.applied().size() == 3);
    REQUIRE(r.valueFor(OscCommandKind::MasterVolume) == Approx(0.5f));
    // Track 1's last value was 4.9, clamped; track 2's was 0.49 and was not.
    REQUIRE(r.valueFor(OscCommandKind::TrackVolume, 1) == Approx(1.0f));
    REQUIRE(r.valueFor(OscCommandKind::TrackVolume, 2) == Approx(0.49f));
}

TEST_CASE("A value arriving during a drain is not stranded", "[osc][router]") {
    // The drain releases its scheduled flag before walking the table rather
    // than after. The difference only shows for a value published while a drain
    // is already running — the ordinary case for a surface that never stops
    // sending. If that value could not schedule the next drain, the last
    // position of a fader the user has just let go of would sit unapplied until
    // they touched it again.
    //
    // A sink that sends from inside `apply` reproduces exactly that window
    // without a second thread. It re-sends to a *lower* slot than the one being
    // applied, which the walk in progress has already passed, so nothing but a
    // freshly scheduled drain can deliver it.
    class ResendingSink : public OscCommandSink {
      public:
        void apply(const OscCommand& command, float value) override {
            applied.push_back({command, value});
            if (router != nullptr && !resent) {
                resent = true;
                router->handleMessage(juce::OSCMessage("/magda/track/1/volume", 0.8f));
            }
        }

        OscRouter* router = nullptr;
        bool resent = false;
        std::vector<Applied> applied;
    };

    auto owned = std::make_unique<ResendingSink>();
    auto* sink = owned.get();
    OscRouter router(std::move(owned));
    sink->router = &router;

    int drainsRequested = 0;
    router.setDrainScheduler([&drainsRequested]() { ++drainsRequested; });

    router.handleMessage(juce::OSCMessage("/magda/track/2/volume", 0.2f));
    REQUIRE(drainsRequested == 1);

    router.drainPending();
    REQUIRE(sink->applied.size() == 1);  // track 1's value is still in the table
    REQUIRE(drainsRequested == 2);       // ...and a drain was asked for to collect it

    router.drainPending();
    REQUIRE(sink->applied.size() == 2);
    REQUIRE(sink->applied[1].command.index == 1);
    REQUIRE(sink->applied[1].value == Approx(0.8f));
}

TEST_CASE("Draining an empty table applies nothing", "[osc][router]") {
    HeldRouter r;
    r.drain();
    r.drain();
    REQUIRE(r.applied().empty());
}

// ============================================================================
// The whole addressable surface
// ============================================================================

TEST_CASE("Every fixed-namespace address survives a round trip", "[osc][router]") {
    // One message per addressable control, all pending at once: the widest the
    // table ever gets, and the check that no two of them collide on a slot.
    HeldRouter r;
    int sent = 0;

    auto send = [&](const juce::String& address, float value) {
        REQUIRE(r.router->handleMessage(juce::OSCMessage(address, value)));
        ++sent;
    };

    send("/magda/transport/record", 1.0f);
    send("/magda/transport/loop", 1.0f);
    send("/magda/transport/tempo", 128.0f);
    send("/magda/transport/position", 16.0f);
    send("/magda/master/volume", 0.8f);
    send("/magda/master/pan", 0.5f);
    for (int macro = 1; macro <= kMaxMacroNumber; ++macro)
        send("/magda/focused/macro/" + juce::String(macro), 0.25f);
    for (int track = 1; track <= kMaxTrackNumber; ++track) {
        send("/magda/track/" + juce::String(track) + "/volume", 0.7f);
        send("/magda/track/" + juce::String(track) + "/pan", 0.5f);
        send("/magda/track/" + juce::String(track) + "/mute", 0.0f);
        send("/magda/track/" + juce::String(track) + "/solo", 0.0f);
        for (int bus = 1; bus <= kMaxSendNumber; ++bus)
            send("/magda/track/" + juce::String(track) + "/send/" + juce::String(bus), 0.3f);
    }

    r.drain();
    REQUIRE(static_cast<int>(r.applied().size()) == sent);
    // Every discrete address pressed at once has to fit: this is the state dump
    // a surface sends on connect, not a flood.
    REQUIRE(r.router->droppedCommandCount() == 0);
}

TEST_CASE("Repeated seeks all survive the drain", "[osc][router]") {
    // The reason a relative seek is not a coalesced value. Three presses of a
    // rewind button are three bars; latest-value-wins would keep one of them
    // and the playhead would move a third of the way the user asked for.
    //
    // A locate in the same breath is the opposite: it says where to be, so
    // only the last one matters and it is still coalesced.
    HeldRouter r;
    r.send(juce::OSCMessage("/magda/transport/seek/bars", -1.0f));
    r.send(juce::OSCMessage("/magda/transport/seek/bars", -1.0f));
    r.send(juce::OSCMessage("/magda/transport/seek/bars", -1.0f));
    r.drain();

    REQUIRE(r.applied().size() == 3);
    for (const auto& entry : r.applied()) {
        REQUIRE(entry.command.kind == OscCommandKind::TransportSeekBars);
        REQUIRE(entry.value == Approx(-1.0f));
    }
}

TEST_CASE("A seek lands after the locate it was sent with", "[osc][router]") {
    // Both in one bundle: the locate is a position and the seek is a distance
    // from wherever the playhead ended up, so the seek has to see the locate's
    // result. The drain applies coalesced values before ordered ones, which is
    // what puts them in that order.
    HeldRouter r;
    r.send(juce::OSCMessage("/magda/transport/seek", 2.5f));
    r.send(juce::OSCMessage("/magda/transport/position", 64.0f));
    r.drain();

    REQUIRE(r.applied().size() == 2);
    REQUIRE(r.applied()[0].command.kind == OscCommandKind::TransportPosition);
    REQUIRE(r.applied()[1].command.kind == OscCommandKind::TransportSeekBeats);
    REQUIRE(r.applied()[1].value == Approx(2.5f));
}

TEST_CASE("A seek with nothing to carry is declined", "[osc][router]") {
    // A bare address says nothing about how far, and a zero delta says to move
    // nowhere. Neither is a release edge to be swallowed silently — there is
    // simply nothing to do, and applying either would be inventing a distance.
    HeldRouter r;
    REQUIRE_FALSE(r.send(juce::OSCMessage("/magda/transport/seek")));
    REQUIRE_FALSE(r.send(juce::OSCMessage("/magda/transport/seek/bars", 0.0f)));
    r.drain();

    REQUIRE(r.applied().empty());
}

TEST_CASE("A non-finite seek is declined at the router", "[osc][router]") {
    // Refused where the packet is read, so nothing downstream has to hold a
    // value that cannot be rounded.
    HeldRouter r;
    REQUIRE_FALSE(
        r.send(juce::OSCMessage("/magda/transport/seek", std::numeric_limits<float>::quiet_NaN())));
    REQUIRE_FALSE(r.send(
        juce::OSCMessage("/magda/transport/seek/bars", std::numeric_limits<float>::infinity())));
    r.drain();

    REQUIRE(r.applied().empty());
}
