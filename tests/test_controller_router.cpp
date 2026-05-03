#include <juce_events/juce_events.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/audio/controllers/ControllerRouter.hpp"
#include "../magda/daw/core/SelectionManager.hpp"
#include "../magda/daw/core/aliases/AliasRegistry.hpp"
#include "../magda/daw/core/aliases/ChainContext.hpp"
#include "../magda/daw/core/controllers/BindingRegistry.hpp"
#include "../magda/daw/core/controllers/ControllerRegistry.hpp"

using namespace magda;
using Catch::Approx;

// ============================================================================
// Fake param writer for testing
// ============================================================================

struct WriteCapture {
    ResolveResult result;
    float value = 0.0f;
};

class FakeParamWriter : public ControllerParamWriter {
  public:
    std::vector<WriteCapture> writes;

    void write(const ResolveResult& resolved, float value) override {
        writes.push_back({resolved, value});
    }
};

// ============================================================================
// Helpers
// ============================================================================

static void clearRegistries() {
    auto& cr = ControllerRegistry::getInstance();
    for (const auto& c : cr.all())
        cr.remove(c.id);

    auto& br = BindingRegistry::getInstance();
    for (const auto& b : br.bindings(BindingScope::Global))
        br.remove(BindingScope::Global, b.id);
    br.clearProject();
}

// In test context (no message loop running), the router executes writes
// synchronously on the calling thread, so no pumping is needed.
// This stub is kept in case a message loop is present in integration tests.
static void pumpMessages(int ms = 1) {
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    if (mm)
        mm->runDispatchLoopUntil(ms);
}

// Build a ControlTarget pointing to a known device/param
static Target makeControlTarget() {
    ControlTarget st;
    st.devicePath = ChainNodePath::topLevelDevice(1, 10);
    st.paramIndex = 0;
    return Target{st};
}

static Controller makeController(const juce::String& port) {
    Controller c;
    c.id = juce::Uuid();
    c.name = "Test Controller";
    c.inputPort = port;
    return c;
}

static Binding makeBinding(const ControllerId& cid, BindingMsgType msgType, int channel, int number,
                           BindingMode mode = BindingMode::Absolute) {
    Binding b;
    b.id = juce::Uuid();
    b.source.controllerId = cid;
    b.source.msgType = msgType;
    b.source.channel = channel;
    b.source.number = number;
    b.target = makeControlTarget();
    b.mode = mode;
    b.range = BindingRange{0.0f, 1.0f, BindingCurve::Linear};
    return b;
}

// ============================================================================
// Fixture setup / teardown
// ============================================================================

struct RouterFixture {
    RouterFixture() {
        clearRegistries();
        auto& router = ControllerRouter::getInstance();
        router.shutdown();

        // Select the track that test targets are pinned to (track 1).
        // ControllerRouter::executeWrite gates track-pinned bindings on the
        // selected track via SelectionManager.
        SelectionManager::getInstance().selectTrack(1);

        writer = new FakeParamWriter();
        router.setParamWriter(std::unique_ptr<ControllerParamWriter>(writer));
        router.reconfigure();
    }

    ~RouterFixture() {
        ControllerRouter::getInstance().shutdown();
        SelectionManager::getInstance().selectTrack(INVALID_TRACK_ID);
        // writer is owned by router now; don't delete
        clearRegistries();
    }

    FakeParamWriter* writer = nullptr;  // borrowed (owned by router)
};

// ============================================================================
// Tests
// ============================================================================

TEST_CASE("ControllerRouter - enabled controller, Absolute CC -> one write",
          "[controllers][router]") {
    RouterFixture fix;

    auto c = makeController("test_port_1");
    ControllerRegistry::getInstance().add(c);

    auto b = makeBinding(c.id, BindingMsgType::CC, 1, 74, BindingMode::Absolute);
    BindingRegistry::getInstance().add(BindingScope::Global, b);

    // ControlTarget with no real plugin -- resolver will fail to find it, so write
    // will be skipped by the real writer. But the fake writer doesn't care about
    // resolved.ok(). Let's verify the fake writer receives the call with the
    // correct resolved target (even though ok() is false in test context).
    // The ResolveResult for a ControlTarget always sets resolved=true and
    // copies the path/paramIndex, even in tests without a live DAW.
    auto msg = juce::MidiMessage::controllerEvent(1, 74, 64);  // CC 74 ch1 value 64
    ControllerRouter::getInstance().injectMessageForTest("test_port_1", msg);

    pumpMessages();

    // Fake writer doesn't call write() because resolved.ok() is false
    // (no live plugin behind the ControlTarget). But the TargetResolver for
    // ControlTarget always returns resolved=true. Let's verify:
    // ControlTarget::isValid() just checks path and paramIndex. Since both
    // are set, ResolveResult::ok() returns true. So write() IS called.
    REQUIRE(fix.writer->writes.size() == 1);
    REQUIRE(fix.writer->writes[0].value == Approx(64.0f / 127.0f));
}

TEST_CASE("ControllerRouter - unknown CC -> no writes", "[controllers][router]") {
    RouterFixture fix;

    auto c = makeController("test_port_3");
    ControllerRegistry::getInstance().add(c);

    auto b = makeBinding(c.id, BindingMsgType::CC, 1, 74);
    BindingRegistry::getInstance().add(BindingScope::Global, b);

    // Send CC 99 which has no binding
    auto msg = juce::MidiMessage::controllerEvent(1, 99, 64);
    ControllerRouter::getInstance().injectMessageForTest("test_port_3", msg);

    pumpMessages();

    REQUIRE(fix.writer->writes.empty());
}

TEST_CASE("ControllerRouter - two controllers, same CC, two writes", "[controllers][router]") {
    RouterFixture fix;

    auto c1 = makeController("test_port_4a");
    auto c2 = makeController("test_port_4b");
    ControllerRegistry::getInstance().add(c1);
    ControllerRegistry::getInstance().add(c2);

    auto b1 = makeBinding(c1.id, BindingMsgType::CC, 1, 74);
    auto b2 = makeBinding(c2.id, BindingMsgType::CC, 1, 74);
    BindingRegistry::getInstance().add(BindingScope::Global, b1);
    BindingRegistry::getInstance().add(BindingScope::Global, b2);

    auto msg = juce::MidiMessage::controllerEvent(1, 74, 100);

    ControllerRouter::getInstance().injectMessageForTest("test_port_4a", msg);
    ControllerRouter::getInstance().injectMessageForTest("test_port_4b", msg);

    pumpMessages();

    REQUIRE(fix.writer->writes.size() == 2);
}

TEST_CASE("ControllerRouter - Toggle rising edge fires only once", "[controllers][router]") {
    RouterFixture fix;

    auto c = makeController("test_port_5");
    ControllerRegistry::getInstance().add(c);

    auto b = makeBinding(c.id, BindingMsgType::CC, 1, 1, BindingMode::Toggle);
    BindingRegistry::getInstance().add(BindingScope::Global, b);

    // First high: toggles to on -> write with value 1.0
    auto msgHigh = juce::MidiMessage::controllerEvent(1, 1, 127);
    ControllerRouter::getInstance().injectMessageForTest("test_port_5", msgHigh);
    pumpMessages();

    REQUIRE(fix.writer->writes.size() == 1);
    REQUIRE(fix.writer->writes[0].value == Approx(1.0f));

    // Second high without drop: no re-trigger (value unchanged)
    ControllerRouter::getInstance().injectMessageForTest("test_port_5", msgHigh);
    pumpMessages();

    // Still only 1 unique trigger (but callAsync will fire again with same value)
    // The toggle state doesn't flip on second consecutive high
    // All writes have been collected; the last one should still be 1.0
    for (const auto& w : fix.writer->writes) {
        REQUIRE(w.value == Approx(1.0f));
    }

    // Drop below 64 to re-arm
    auto msgLow = juce::MidiMessage::controllerEvent(1, 1, 0);
    ControllerRouter::getInstance().injectMessageForTest("test_port_5", msgLow);
    pumpMessages();

    size_t beforeRearm = fix.writer->writes.size();

    // Rising edge again: toggles to off -> write with value 0.0
    ControllerRouter::getInstance().injectMessageForTest("test_port_5", msgHigh);
    pumpMessages();

    REQUIRE(fix.writer->writes.size() > beforeRearm);
    REQUIRE(fix.writer->writes.back().value == Approx(0.0f));
}

TEST_CASE("ControllerRouter - static PluginParam shadows focused-device-macro resolver on same CC",
          "[controllers][router]") {
    RouterFixture fix;

    auto c = makeController("test_port_shadow");
    ControllerRegistry::getInstance().add(c);

    // Focus a device so the focused.macro resolver actually resolves.
    auto devicePath = ChainNodePath::topLevelDevice(1, 10);
    SelectionManager::getInstance().selectChainNode(devicePath);

    // Profile-style binding: CC 21 -> @focused.macro_0 (macro target)
    Binding bMacro = makeBinding(c.id, BindingMsgType::CC, 1, 21);
    ResolverRef rr;
    rr.kind = "focused.macro";
    rr.args.set("macroIndex", "0");
    bMacro.target = Target{rr};
    BindingRegistry::getInstance().add(BindingScope::Project, bMacro);

    // Learn-style binding: CC 21 -> static PluginParam on the same controller
    Binding bParam = makeBinding(c.id, BindingMsgType::CC, 1, 21);
    ControlTarget st;
    st.devicePath = devicePath;
    st.paramIndex = 5;
    st.kind = ControlTarget::Kind::PluginParam;
    bParam.target = Target{st};
    BindingRegistry::getInstance().add(BindingScope::Project, bParam);

    auto msg = juce::MidiMessage::controllerEvent(1, 21, 100);
    ControllerRouter::getInstance().injectMessageForTest("test_port_shadow", msg);
    pumpMessages();

    // Only the static PluginParam binding should fire.
    REQUIRE(fix.writer->writes.size() == 1);
    REQUIRE(fix.writer->writes[0].result.target.kind == ControlTarget::Kind::PluginParam);
    REQUIRE(fix.writer->writes[0].result.target.paramIndex == 5);

    SelectionManager::getInstance().clearChainNodeSelection();
}

TEST_CASE("BindingRegistry - shadow + override queries flag both sides of conflict",
          "[controllers][binding_registry]") {
    // Reset registry state so this test is self-contained alongside the router fixtures.
    {
        auto& cr = ControllerRegistry::getInstance();
        for (const auto& c : cr.all())
            cr.remove(c.id);
        auto& br = BindingRegistry::getInstance();
        for (const auto& b : br.bindings(BindingScope::Global))
            br.remove(BindingScope::Global, b.id);
        br.clearProject();
    }

    auto c = makeController("test_port_override");
    ControllerRegistry::getInstance().add(c);

    auto devicePath = ChainNodePath::topLevelDevice(1, 10);
    SelectionManager::getInstance().selectTrack(1);
    SelectionManager::getInstance().selectChainNode(devicePath);

    auto& reg = BindingRegistry::getInstance();

    // Profile binding: CC 21 -> @focused.macro_0
    Binding bMacro = makeBinding(c.id, BindingMsgType::CC, 1, 21);
    ResolverRef rr;
    rr.kind = "focused.macro";
    rr.args.set("macroIndex", "0");
    bMacro.target = Target{rr};
    reg.add(BindingScope::Project, bMacro);

    // No conflict yet.
    REQUIRE_FALSE(reg.isAutomapShadowedForMacro(devicePath, 0));
    REQUIRE_FALSE(reg.isPluginParamOverridingMacro(devicePath, 5));

    // Add the Learn override: CC 21 -> static PluginParam on the same controller.
    Binding bParam = makeBinding(c.id, BindingMsgType::CC, 1, 21);
    ControlTarget st;
    st.devicePath = devicePath;
    st.paramIndex = 5;
    st.kind = ControlTarget::Kind::PluginParam;
    bParam.target = Target{st};
    reg.add(BindingScope::Project, bParam);

    REQUIRE(reg.isAutomapShadowedForMacro(devicePath, 0));
    REQUIRE(reg.isPluginParamOverridingMacro(devicePath, 5));

    // Unrelated macroIndex is unaffected.
    REQUIRE_FALSE(reg.isAutomapShadowedForMacro(devicePath, 1));
    REQUIRE_FALSE(reg.isPluginParamOverridingMacro(devicePath, 6));

    // Removing the Learn override clears both flags.
    reg.remove(BindingScope::Project, bParam.id);
    REQUIRE_FALSE(reg.isAutomapShadowedForMacro(devicePath, 0));
    REQUIRE_FALSE(reg.isPluginParamOverridingMacro(devicePath, 5));

    SelectionManager::getInstance().clearChainNodeSelection();
}
