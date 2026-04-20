#include <juce_events/juce_events.h>

#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/audio/ControllerRouter.hpp"
#include "../magda/daw/audio/MidiLearnSession.hpp"
#include "../magda/daw/core/aliases/AliasRegistry.hpp"
#include "../magda/daw/core/aliases/AliasReverseIndex.hpp"
#include "../magda/daw/core/controllers/BindingRegistry.hpp"
#include "../magda/daw/core/controllers/ControllerRegistry.hpp"

using namespace magda;

// ============================================================================
// Helpers
// ============================================================================

static void clearAll() {
    auto& cr = ControllerRegistry::getInstance();
    for (const auto& c : cr.all())
        cr.remove(c.id);

    auto& br = BindingRegistry::getInstance();
    for (const auto& b : br.bindings(BindingScope::Global))
        br.remove(BindingScope::Global, b.id);
    br.clearProject();

    auto& ar = AliasRegistry::getInstance();
    ar.clearLayer(AliasLayer::AutoGen);
    ar.clearLayer(AliasLayer::UserProject);
    ar.clearLayer(AliasLayer::UserGlobal);
    ar.clearLayer(AliasLayer::Curated);
}

static Controller makeController(const juce::String& port,
                                 const juce::String& name = "Test Controller") {
    Controller c;
    c.id = juce::Uuid();
    c.name = name;
    c.inputPort = port;
    c.enabled = true;
    return c;
}

static Binding makeStaticBinding(const ControllerId& cid, BindingMsgType msgType, int channel,
                                 int number, const ChainNodePath& path, int paramIndex) {
    Binding b;
    b.id = juce::Uuid();
    b.source.controllerId = cid;
    b.source.msgType = msgType;
    b.source.channel = channel;
    b.source.number = number;
    StaticTarget st;
    st.devicePath = path;
    st.paramIndex = paramIndex;
    b.target = Target{st};
    b.mode = BindingMode::Absolute;
    b.range = BindingRange{0.0f, 1.0f, BindingCurve::Linear};
    return b;
}

// Simple "no real plugin" param writer
class NullWriter : public ControllerParamWriter {
  public:
    void write(const ResolvedTarget&, float) override {}
};

struct RouterLearnFixture {
    RouterLearnFixture() {
        clearAll();
        auto& router = ControllerRouter::getInstance();
        router.shutdown();
        router.setParamWriter(std::make_unique<NullWriter>());
        router.reconfigure();
    }

    ~RouterLearnFixture() {
        ControllerRouter::getInstance().cancelLearnSession();
        ControllerRouter::getInstance().shutdown();
        clearAll();
    }
};

// ============================================================================
// Tests: beginLearnSession + CC capture
// ============================================================================

TEST_CASE("MidiLearn - begin + CC capture fires callback with correct fields",
          "[midi-learn][router]") {
    RouterLearnFixture fix;

    auto c = makeController("learn_port_1");
    ControllerRegistry::getInstance().add(c);

    auto& router = ControllerRouter::getInstance();

    bool callbackFired = false;
    LearnCapture captured{};

    LearnSessionConfig cfg;
    router.beginLearnSession(cfg, [&](const LearnCapture& cap) {
        callbackFired = true;
        captured = cap;
    });

    REQUIRE(router.isLearning());

    auto msg = juce::MidiMessage::controllerEvent(1, 21, 100);
    router.injectMessageForTest("learn_port_1", msg);

    REQUIRE(callbackFired);
    REQUIRE(!router.isLearning());
    REQUIRE(captured.portId == "learn_port_1");
    REQUIRE(captured.msgType == BindingMsgType::CC);
    REQUIRE(captured.number == 21);
    REQUIRE(captured.rawValue == 100);
    REQUIRE(captured.channel == 1);
    REQUIRE(captured.controllerId == c.id);
}

// ============================================================================
// Tests: cancel
// ============================================================================

TEST_CASE("MidiLearn - cancel prevents callback from firing", "[midi-learn][router]") {
    RouterLearnFixture fix;

    auto c = makeController("learn_port_2");
    ControllerRegistry::getInstance().add(c);

    auto& router = ControllerRouter::getInstance();

    bool fired = false;
    LearnSessionConfig cfg;
    router.beginLearnSession(cfg, [&](const LearnCapture&) { fired = true; });

    REQUIRE(router.isLearning());

    router.cancelLearnSession();

    REQUIRE(!router.isLearning());

    auto msg = juce::MidiMessage::controllerEvent(1, 21, 50);
    router.injectMessageForTest("learn_port_2", msg);

    REQUIRE(!fired);
}

// ============================================================================
// Tests: re-arm (second beginLearn replaces first)
// ============================================================================

TEST_CASE("MidiLearn - re-arm: second beginLearn replaces first callback", "[midi-learn][router]") {
    RouterLearnFixture fix;

    auto c = makeController("learn_port_3");
    ControllerRegistry::getInstance().add(c);

    auto& router = ControllerRouter::getInstance();

    bool firedA = false;
    bool firedB = false;

    LearnSessionConfig cfg;
    router.beginLearnSession(cfg, [&](const LearnCapture&) { firedA = true; });
    router.beginLearnSession(cfg, [&](const LearnCapture&) { firedB = true; });

    REQUIRE(router.isLearning());

    auto msg = juce::MidiMessage::controllerEvent(1, 10, 64);
    router.injectMessageForTest("learn_port_3", msg);

    REQUIRE(!firedA);
    REQUIRE(firedB);
}

// ============================================================================
// Tests: non-qualifying message (program change) keeps session armed
// ============================================================================

TEST_CASE("MidiLearn - program change while armed does not capture", "[midi-learn][router]") {
    RouterLearnFixture fix;

    auto c = makeController("learn_port_4");
    ControllerRegistry::getInstance().add(c);

    auto& router = ControllerRouter::getInstance();

    bool fired = false;
    LearnSessionConfig cfg;
    router.beginLearnSession(cfg, [&](const LearnCapture&) { fired = true; });

    // Program change is non-qualifying — session stays armed
    auto pcMsg = juce::MidiMessage::programChange(1, 5);
    router.injectMessageForTest("learn_port_4", pcMsg);

    REQUIRE(!fired);
    REQUIRE(router.isLearning());  // still armed

    // Now a CC arrives and captures
    auto ccMsg = juce::MidiMessage::controllerEvent(1, 7, 80);
    router.injectMessageForTest("learn_port_4", ccMsg);

    REQUIRE(fired);
    REQUIRE(!router.isLearning());
}

// ============================================================================
// Tests: Note-off debounce
// ============================================================================

TEST_CASE("MidiLearn - Note-off within debounce window is dropped", "[midi-learn][router]") {
    RouterLearnFixture fix;

    auto c = makeController("learn_port_5");
    ControllerRegistry::getInstance().add(c);

    auto& router = ControllerRouter::getInstance();

    int callCount = 0;
    BindingMsgType lastType = BindingMsgType::CC;

    LearnSessionConfig cfg;
    cfg.captureDebounceMs = 500;  // large window so the Note-off is within it
    router.beginLearnSession(cfg, [&](const LearnCapture& cap) {
        ++callCount;
        lastType = cap.msgType;
    });

    // Note-on should be captured
    auto noteOn = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    router.injectMessageForTest("learn_port_5", noteOn);

    REQUIRE(callCount == 1);
    REQUIRE(lastType == BindingMsgType::Note);
    REQUIRE(!router.isLearning());

    // Now re-arm and check that a Note-off within the window is dropped
    router.beginLearnSession(cfg, [&](const LearnCapture& cap) {
        ++callCount;
        lastType = cap.msgType;
    });

    router.injectMessageForTest("learn_port_5", noteOn);  // Note-on: captured -> session done
    REQUIRE(callCount == 2);

    // Start a fresh session: inject Note-off immediately (< 500ms) -> dropped
    router.beginLearnSession(cfg, [&](const LearnCapture& cap) {
        ++callCount;
        lastType = cap.msgType;
    });

    auto noteOff = juce::MidiMessage::noteOff(1, 60, (juce::uint8)0);
    router.injectMessageForTest("learn_port_5", noteOff);

    // Note-off dropped (debounce), session still armed
    REQUIRE(callCount == 2);
    REQUIRE(router.isLearning());

    router.cancelLearnSession();
}

// ============================================================================
// Tests: AliasReverseIndex - findByPath + autoGenOnly
// ============================================================================

TEST_CASE("AliasReverseIndex - findAliasesByPath autoGenOnly=true returns only AutoGen",
          "[midi-learn][aliases]") {
    clearAll();

    auto& reg = AliasRegistry::getInstance();

    ChainNodePath path1 = ChainNodePath::topLevelDevice(1, 10);
    ChainNodePath path2 = ChainNodePath::topLevelDevice(2, 20);

    StoredAlias alias1;
    alias1.pluginTypeKey = "serum";
    alias1.paramIndex = 3;
    alias1.paramNameAtSetTime = "Filter Cutoff";
    alias1.path = path1;

    StoredAlias alias2;
    alias2.pluginTypeKey = "surge_xt";
    alias2.paramIndex = 7;
    alias2.paramNameAtSetTime = "Osc Pitch";
    alias2.path = path2;

    reg.set(AliasLayer::AutoGen, "serum.filter_cutoff", alias1);
    reg.set(AliasLayer::AutoGen, "surge_xt.osc_pitch", alias2);

    // UserProject entry for path1 that should NOT appear with autoGenOnly=true
    StoredAlias aliasUser;
    aliasUser.pluginTypeKey = "serum";
    aliasUser.paramIndex = 3;
    aliasUser.paramNameAtSetTime = "Filter Cutoff";
    aliasUser.path = path1;
    reg.set(AliasLayer::UserProject, "my_cutoff", aliasUser);

    // autoGenOnly=true: only AutoGen layer
    auto matches = findAliasesByPath(reg, path1, 3, true);
    REQUIRE(matches.size() == 1);
    REQUIRE(matches[0].canonicalName == "serum.filter_cutoff");
    REQUIRE(matches[0].layer == AliasLayer::AutoGen);

    // autoGenOnly=false: all layers — should find both AutoGen and UserProject
    auto allMatches = findAliasesByPath(reg, path1, 3, false);
    REQUIRE(allMatches.size() == 2);

    // bestAliasForPath prefers highest-priority layer (UserProject=0 < AutoGen=3)
    auto best = bestAliasForPath(reg, path1, 3, false);
    REQUIRE(best.has_value());
    REQUIRE(*best == "my_cutoff");

    // path2 has no UserProject entry -> only AutoGen
    auto matches2 = findAliasesByPath(reg, path2, 7, false);
    REQUIRE(matches2.size() == 1);
    REQUIRE(matches2[0].canonicalName == "surge_xt.osc_pitch");

    clearAll();
}

// ============================================================================
// Tests: BindingRegistry::findForTarget + removeForTarget
// ============================================================================

TEST_CASE("BindingRegistry - findForTarget returns bindings resolving to (path, paramIndex)",
          "[midi-learn][bindings]") {
    clearAll();

    ChainNodePath targetPath = ChainNodePath::topLevelDevice(5, 50);
    int targetParam = 2;

    // Seed two controllers
    auto c1 = makeController("bind_port_1");
    auto c2 = makeController("bind_port_2");
    ControllerRegistry::getInstance().add(c1);
    ControllerRegistry::getInstance().add(c2);

    // One Global binding -> (targetPath, targetParam)
    auto bGlobal = makeStaticBinding(c1.id, BindingMsgType::CC, 0, 21, targetPath, targetParam);
    BindingRegistry::getInstance().add(BindingScope::Global, bGlobal);

    // One Project binding -> (targetPath, targetParam)
    auto bProject = makeStaticBinding(c2.id, BindingMsgType::CC, 0, 22, targetPath, targetParam);
    BindingRegistry::getInstance().add(BindingScope::Project, bProject);

    // One Global binding -> different path (should NOT match)
    ChainNodePath otherPath = ChainNodePath::topLevelDevice(6, 60);
    auto bOther = makeStaticBinding(c1.id, BindingMsgType::CC, 0, 23, otherPath, 0);
    BindingRegistry::getInstance().add(BindingScope::Global, bOther);

    auto found = BindingRegistry::getInstance().findForTarget(targetPath, targetParam);
    REQUIRE(found.size() == 2);

    // Both ids should be present
    bool foundGlobal = false;
    bool foundProject = false;
    for (const auto& b : found) {
        if (b.id == bGlobal.id)
            foundGlobal = true;
        if (b.id == bProject.id)
            foundProject = true;
    }
    REQUIRE(foundGlobal);
    REQUIRE(foundProject);

    clearAll();
}

TEST_CASE("BindingRegistry - removeForTarget removes all matching bindings",
          "[midi-learn][bindings]") {
    clearAll();

    ChainNodePath targetPath = ChainNodePath::topLevelDevice(7, 70);
    int targetParam = 4;

    auto c = makeController("bind_port_3");
    ControllerRegistry::getInstance().add(c);

    auto b1 = makeStaticBinding(c.id, BindingMsgType::CC, 0, 10, targetPath, targetParam);
    auto b2 = makeStaticBinding(c.id, BindingMsgType::CC, 0, 11, targetPath, targetParam);
    BindingRegistry::getInstance().add(BindingScope::Global, b1);
    BindingRegistry::getInstance().add(BindingScope::Project, b2);

    int removed = BindingRegistry::getInstance().removeForTarget(targetPath, targetParam);
    REQUIRE(removed == 2);

    // After removal, findForTarget returns empty
    auto remaining = BindingRegistry::getInstance().findForTarget(targetPath, targetParam);
    REQUIRE(remaining.empty());

    clearAll();
}
