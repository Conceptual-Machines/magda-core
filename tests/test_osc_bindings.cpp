#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include "../magda/daw/audio/osc/OscRouter.hpp"
#include "../magda/daw/core/controllers/Binding.hpp"

using namespace magda;
using namespace magda::osc;
using Catch::Approx;

namespace {

Binding oscBinding(const juce::String& address, int argIndex = 0) {
    Binding b;
    b.id = juce::Uuid();
    b.source.kind = BindingSourceKind::Osc;
    b.source.oscAddress = address;
    b.source.oscArgIndex = argIndex;
    b.target = ControlTarget::trackVolume(1);
    return b;
}

Binding midiBinding() {
    Binding b;
    b.id = juce::Uuid();
    b.source.portKey = "Launch Control";
    b.source.number = 21;
    b.target = ControlTarget::trackVolume(1);
    return b;
}

struct Applied {
    Binding binding;
    float value = 0.0f;
};

class RecordingBindingSink : public OscBindingSink {
  public:
    void apply(const Binding& binding, float value) override {
        applied.push_back({binding, value});
    }

    std::vector<Applied> applied;
};

class IgnoringCommandSink : public OscCommandSink {
  public:
    void apply(const OscCommand&, float) override {
        ++applied;
    }
    int applied = 0;
};

struct Harness {
    Harness() {
        auto ownedCommands = std::make_unique<IgnoringCommandSink>();
        commands = ownedCommands.get();
        router = std::make_unique<OscRouter>(std::move(ownedCommands));

        auto ownedBindings = std::make_unique<RecordingBindingSink>();
        bindings = ownedBindings.get();
        router->setBindingSink(std::move(ownedBindings));
        router->setDrainScheduler([]() {});
    }

    IgnoringCommandSink* commands = nullptr;
    RecordingBindingSink* bindings = nullptr;
    std::unique_ptr<OscRouter> router;
};

}  // namespace

// ============================================================================
// The source model
// ============================================================================

TEST_CASE("An OSC source is identified by its address", "[osc][bindings]") {
    auto a = oscBinding("/1/fader1");
    auto b = a;
    REQUIRE(a.source == b.source);

    b.source.oscAddress = "/1/fader2";
    REQUIRE(a.source != b.source);

    // A MIDI source and an OSC source are never the same source, whatever else
    // happens to match.
    Binding midi = midiBinding();
    REQUIRE(midi.source != a.source);
}

TEST_CASE("An OSC binding needs a real address to be valid", "[osc][bindings]") {
    REQUIRE(oscBinding("/1/fader1").isValid());

    auto noSlash = oscBinding("1/fader1");
    REQUIRE_FALSE(noSlash.isValid());

    auto empty = oscBinding("");
    REQUIRE_FALSE(empty.isValid());

    // The MIDI rule is untouched: a port or a controller id, and no address.
    REQUIRE(midiBinding().isValid());
}

TEST_CASE("OSC bindings survive a save and load", "[osc][bindings]") {
    auto original = oscBinding("/1/fader3", /*argIndex*/ 2);
    original.mode = BindingMode::Toggle;
    original.range.min = 0.25f;
    original.range.max = 0.75f;
    original.range.curve = BindingCurve::Exp;

    const auto decoded = decodeBinding(encodeBinding(original));
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->source.kind == BindingSourceKind::Osc);
    REQUIRE(decoded->source.oscAddress == "/1/fader3");
    REQUIRE(decoded->source.oscArgIndex == 2);
    REQUIRE(decoded->mode == BindingMode::Toggle);
    REQUIRE(decoded->range.min == Approx(0.25f));
    REQUIRE(decoded->range.curve == BindingCurve::Exp);
}

TEST_CASE("A binding written before OSC existed still loads as MIDI", "[osc][bindings]") {
    // No "kind" in the source object: every binding in every config file
    // written until now. It has to keep meaning what it meant.
    auto encoded = encodeBinding(midiBinding());
    auto* source = encoded.getDynamicObject()->getProperty("source").getDynamicObject();
    REQUIRE(source != nullptr);
    REQUIRE_FALSE(source->hasProperty("kind"));

    const auto decoded = decodeBinding(encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->source.kind == BindingSourceKind::Midi);
    REQUIRE(decoded->source.portKey == "Launch Control");
    REQUIRE(decoded->source.number == 21);
}

// ============================================================================
// Routing
// ============================================================================

TEST_CASE("A bound address reaches the binding sink", "[osc][bindings]") {
    Harness h;
    const auto binding = oscBinding("/1/fader1");
    h.router->updateBindings({binding});

    REQUIRE(h.router->handleMessage(juce::OSCMessage("/1/fader1", 0.4f)));
    h.router->drainPending();

    REQUIRE(h.bindings->applied.size() == 1);
    REQUIRE(h.bindings->applied[0].binding.id == binding.id);
    REQUIRE(h.bindings->applied[0].value == Approx(0.4f));
}

TEST_CASE("An unbound address is still declined", "[osc][bindings]") {
    Harness h;
    h.router->updateBindings({oscBinding("/1/fader1")});

    REQUIRE_FALSE(h.router->handleMessage(juce::OSCMessage("/1/fader2", 0.4f)));
    h.router->drainPending();
    REQUIRE(h.bindings->applied.empty());
}

TEST_CASE("The fixed namespace cannot be bound over", "[osc][bindings]") {
    // A binding that claims a /magda/ address must not shadow it: those are a
    // published contract, and a stock template half-working with no way to see
    // why is worse than the binding simply never firing.
    Harness h;
    h.router->updateBindings({oscBinding("/magda/track/1/volume")});

    REQUIRE(h.router->handleMessage(juce::OSCMessage("/magda/track/1/volume", 0.4f)));
    h.router->drainPending();

    REQUIRE(h.commands->applied == 1);
    REQUIRE(h.bindings->applied.empty());
}

TEST_CASE("Several bindings can share one address", "[osc][bindings]") {
    // One fader driving two parameters is a legitimate mapping, and the route
    // table stores them as a contiguous run rather than one winning.
    Harness h;
    const auto first = oscBinding("/1/fader1");
    const auto second = oscBinding("/1/fader1");
    h.router->updateBindings({first, second});

    REQUIRE(h.router->handleMessage(juce::OSCMessage("/1/fader1", 0.6f)));
    h.router->drainPending();

    REQUIRE(h.bindings->applied.size() == 2);
    REQUIRE(h.bindings->applied[0].value == Approx(0.6f));
    REQUIRE(h.bindings->applied[1].value == Approx(0.6f));
}

TEST_CASE("A binding reads the argument it was learned from", "[osc][bindings]") {
    Harness h;
    h.router->updateBindings({oscBinding("/xy", /*argIndex*/ 1)});

    REQUIRE(h.router->handleMessage(juce::OSCMessage("/xy", 0.1f, 0.9f)));
    h.router->drainPending();

    REQUIRE(h.bindings->applied.size() == 1);
    REQUIRE(h.bindings->applied[0].value == Approx(0.9f));
}

TEST_CASE("A message missing the bound argument does nothing", "[osc][bindings]") {
    Harness h;
    h.router->updateBindings({oscBinding("/xy", /*argIndex*/ 3)});

    REQUIRE_FALSE(h.router->handleMessage(juce::OSCMessage("/xy", 0.1f, 0.9f)));
    h.router->drainPending();
    REQUIRE(h.bindings->applied.empty());
}

TEST_CASE("Bound values are clamped and non-finite ones rejected", "[osc][bindings]") {
    Harness h;
    h.router->updateBindings({oscBinding("/1/fader1")});

    REQUIRE_FALSE(h.router->handleMessage(
        juce::OSCMessage("/1/fader1", std::numeric_limits<float>::quiet_NaN())));
    REQUIRE(h.router->handleMessage(juce::OSCMessage("/1/fader1", 4.0f)));
    h.router->drainPending();

    REQUIRE(h.bindings->applied.size() == 1);
    REQUIRE(h.bindings->applied[0].value == Approx(1.0f));
}

TEST_CASE("A bound fader stream collapses to its latest value", "[osc][bindings]") {
    Harness h;
    h.router->updateBindings({oscBinding("/1/fader1")});

    for (int i = 0; i <= 100; ++i)
        h.router->handleMessage(juce::OSCMessage("/1/fader1", static_cast<float>(i) / 100.0f));
    h.router->drainPending();

    REQUIRE(h.bindings->applied.size() == 1);
    REQUIRE(h.bindings->applied[0].value == Approx(1.0f));
}

TEST_CASE("Removing a binding stops its address firing", "[osc][bindings]") {
    Harness h;
    h.router->updateBindings({oscBinding("/1/fader1")});
    REQUIRE(h.router->handleMessage(juce::OSCMessage("/1/fader1", 0.5f)));
    h.router->drainPending();
    REQUIRE(h.bindings->applied.size() == 1);

    h.router->updateBindings({});
    REQUIRE_FALSE(h.router->handleMessage(juce::OSCMessage("/1/fader1", 0.5f)));
    h.router->drainPending();
    REQUIRE(h.bindings->applied.size() == 1);
}

TEST_CASE("A value published before a re-bind is dropped, not misapplied", "[osc][bindings]") {
    // Indices only mean something against the route list they were resolved
    // against. Replacing the list mid-gesture loses one update; what it must
    // never do is land that update on a different binding's parameter.
    Harness h;
    const auto first = oscBinding("/1/fader1");
    h.router->updateBindings({first});
    REQUIRE(h.router->handleMessage(juce::OSCMessage("/1/fader1", 0.5f)));

    const auto second = oscBinding("/1/fader9");
    h.router->updateBindings({second});
    h.router->drainPending();

    REQUIRE(h.bindings->applied.empty());
}

TEST_CASE("MIDI bindings are not OSC routes", "[osc][bindings]") {
    Harness h;
    h.router->updateBindings({midiBinding()});

    REQUIRE_FALSE(h.router->handleMessage(juce::OSCMessage("/1/fader1", 0.5f)));
    h.router->drainPending();
    REQUIRE(h.bindings->applied.empty());
}

// ============================================================================
// Learn
// ============================================================================

TEST_CASE("Learn captures the next control that moves", "[osc][bindings][learn]") {
    Harness h;
    std::optional<OscLearnCapture> captured;
    h.router->beginLearnSession([&captured](const OscLearnCapture& c) { captured = c; });
    REQUIRE(h.router->isLearning());

    REQUIRE(h.router->handleMessage(juce::OSCMessage("/1/rotary2", 0.33f)));

    REQUIRE(captured.has_value());
    REQUIRE(captured->address == "/1/rotary2");
    REQUIRE(captured->argIndex == 0);
    REQUIRE(captured->value == Approx(0.33f));
    REQUIRE_FALSE(h.router->isLearning());
}

TEST_CASE("A captured gesture does not also drive its binding", "[osc][bindings][learn]") {
    // Wiggling a fader to learn it must not move whatever it is bound to now.
    Harness h;
    h.router->updateBindings({oscBinding("/1/fader1")});

    std::optional<OscLearnCapture> captured;
    h.router->beginLearnSession([&captured](const OscLearnCapture& c) { captured = c; });
    REQUIRE(h.router->handleMessage(juce::OSCMessage("/1/fader1", 0.5f)));
    h.router->drainPending();

    REQUIRE(captured.has_value());
    REQUIRE(h.bindings->applied.empty());
}

TEST_CASE("Learn ignores the fixed namespace", "[osc][bindings][learn]") {
    // Those addresses already mean something, so capturing one would give a
    // control two jobs.
    Harness h;
    std::optional<OscLearnCapture> captured;
    h.router->beginLearnSession([&captured](const OscLearnCapture& c) { captured = c; });

    REQUIRE(h.router->handleMessage(juce::OSCMessage("/magda/track/1/volume", 0.5f)));
    REQUIRE_FALSE(captured.has_value());
    REQUIRE(h.router->isLearning());

    // …and stays armed for a control that is genuinely free.
    REQUIRE(h.router->handleMessage(juce::OSCMessage("/1/fader1", 0.5f)));
    REQUIRE(captured.has_value());
}

TEST_CASE("Learn stays armed through a message it cannot read", "[osc][bindings][learn]") {
    Harness h;
    std::optional<OscLearnCapture> captured;
    h.router->beginLearnSession([&captured](const OscLearnCapture& c) { captured = c; });

    REQUIRE_FALSE(h.router->handleMessage(juce::OSCMessage("/ping")));
    REQUIRE_FALSE(h.router->handleMessage(juce::OSCMessage("/label", juce::String("hello"))));
    REQUIRE(h.router->isLearning());

    REQUIRE(h.router->handleMessage(juce::OSCMessage("/1/fader1", 0.5f)));
    REQUIRE(captured.has_value());
}

TEST_CASE("Learn captures the first argument that carries a number", "[osc][bindings][learn]") {
    Harness h;
    std::optional<OscLearnCapture> captured;
    h.router->beginLearnSession([&captured](const OscLearnCapture& c) { captured = c; });

    REQUIRE(h.router->handleMessage(juce::OSCMessage("/xy", juce::String("pad"), 0.7f)));

    REQUIRE(captured.has_value());
    REQUIRE(captured->argIndex == 1);
    REQUIRE(captured->value == Approx(0.7f));
}

TEST_CASE("A cancelled learn session captures nothing", "[osc][bindings][learn]") {
    Harness h;
    bool fired = false;
    h.router->beginLearnSession([&fired](const OscLearnCapture&) { fired = true; });
    h.router->cancelLearnSession();

    REQUIRE_FALSE(h.router->isLearning());
    REQUIRE_FALSE(h.router->handleMessage(juce::OSCMessage("/1/fader1", 0.5f)));
    REQUIRE_FALSE(fired);
}

TEST_CASE("Starting a learn session cancels the one in progress", "[osc][bindings][learn]") {
    Harness h;
    bool firstFired = false;
    bool secondFired = false;
    h.router->beginLearnSession([&firstFired](const OscLearnCapture&) { firstFired = true; });
    h.router->beginLearnSession([&secondFired](const OscLearnCapture&) { secondFired = true; });

    REQUIRE(h.router->handleMessage(juce::OSCMessage("/1/fader1", 0.5f)));
    REQUIRE_FALSE(firstFired);
    REQUIRE(secondFired);
}
