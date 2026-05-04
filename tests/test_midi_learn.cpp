#include <juce_events/juce_events.h>

#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/audio/controllers/ControllerRouter.hpp"
#include "../magda/daw/audio/midi/MidiLearnSession.hpp"
#include "../magda/daw/core/Config.hpp"
#include "../magda/daw/core/aliases/AliasRegistry.hpp"
#include "../magda/daw/core/aliases/AliasReverseIndex.hpp"
#include "../magda/daw/core/controllers/BindingRegistry.hpp"
#include "../magda/daw/core/controllers/ControllerRegistry.hpp"
#include "../magda/daw/core/controllers/MidiLearnCoordinator.hpp"

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
    ControlTarget st;
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
    void write(const ResolveResult&, float) override {}
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
// Tests: BindingRegistry::findFor + removeFor
// ============================================================================

TEST_CASE("BindingRegistry - findFor returns bindings resolving to a ControlTarget",
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

    auto found =
        BindingRegistry::getInstance().findFor(ControlTarget::pluginParam(targetPath, targetParam));
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

TEST_CASE("BindingRegistry - removeFor removes all matching bindings", "[midi-learn][bindings]") {
    clearAll();

    ChainNodePath targetPath = ChainNodePath::topLevelDevice(7, 70);
    int targetParam = 4;

    auto c = makeController("bind_port_3");
    ControllerRegistry::getInstance().add(c);

    auto b1 = makeStaticBinding(c.id, BindingMsgType::CC, 0, 10, targetPath, targetParam);
    auto b2 = makeStaticBinding(c.id, BindingMsgType::CC, 0, 11, targetPath, targetParam);
    BindingRegistry::getInstance().add(BindingScope::Global, b1);
    BindingRegistry::getInstance().add(BindingScope::Project, b2);

    int removed = BindingRegistry::getInstance().removeFor(
        ControlTarget::pluginParam(targetPath, targetParam));
    REQUIRE(removed == 2);

    // After removal, findFor returns empty
    auto remaining =
        BindingRegistry::getInstance().findFor(ControlTarget::pluginParam(targetPath, targetParam));
    REQUIRE(remaining.empty());

    clearAll();
}

// ============================================================================
// Tests: MidiLearnCoordinator
// ============================================================================

// Fake listener that records all callback invocations
struct FakeCoordinatorListener : public MidiLearnCoordinatorListener {
    struct StateChange {
        ChainNodePath path;
        int paramIndex;
        ControlTarget::Kind kind;
        bool learning;
    };
    struct Completion {
        ChainNodePath path;
        int paramIndex;
        ControlTarget::Kind kind;
        Binding binding;
    };
    struct Cleared {
        ChainNodePath path;
        int paramIndex;
        ControlTarget::Kind kind;
        int numRemoved;
    };

    std::vector<StateChange> stateChanges;
    std::vector<Completion> completions;
    std::vector<Cleared> clears;

    void midiLearnStateChanged(const ChainNodePath& path, int paramIndex, ControlTarget::Kind owner,
                               bool learning) override {
        stateChanges.push_back({path, paramIndex, owner, learning});
    }
    void midiLearnCompleted(const ChainNodePath& path, int paramIndex, ControlTarget::Kind owner,
                            const Binding& binding) override {
        completions.push_back({path, paramIndex, owner, binding});
    }
    void midiLearnCleared(const ChainNodePath& path, int paramIndex, ControlTarget::Kind owner,
                          int numRemoved) override {
        clears.push_back({path, paramIndex, owner, numRemoved});
    }
};

struct CoordinatorFixture {
    CoordinatorFixture() {
        clearAll();
        auto& router = ControllerRouter::getInstance();
        router.shutdown();
        router.setParamWriter(std::make_unique<NullWriter>());
        router.reconfigure();

        auto& coord = MidiLearnCoordinator::getInstance();
        coord.attach(router);
        coord.setScope(BindingScope::Project);
        coord.addListener(&listener);
    }

    ~CoordinatorFixture() {
        MidiLearnCoordinator::getInstance().cancelLearn();
        MidiLearnCoordinator::getInstance().removeListener(&listener);
        ControllerRouter::getInstance().shutdown();
        clearAll();
    }

    FakeCoordinatorListener listener;
};

TEST_CASE(
    "MidiLearnCoordinator - beginLearn + capture creates binding with AliasRef when alias exists",
    "[midi-learn][coordinator]") {
    CoordinatorFixture fix;

    auto c = makeController("coord_port_1");
    ControllerRegistry::getInstance().add(c);

    ChainNodePath path = ChainNodePath::topLevelDevice(10, 100);
    int paramIdx = 5;

    // Seed an AutoGen alias for this path/param
    StoredAlias alias;
    alias.pluginTypeKey = "serum";
    alias.paramIndex = paramIdx;
    alias.paramNameAtSetTime = "Filter Cutoff";
    alias.path = path;
    AliasRegistry::getInstance().set(AliasLayer::AutoGen, "serum.filter_cutoff", alias);

    auto& coord = MidiLearnCoordinator::getInstance();
    coord.beginLearn(ControlTarget::pluginParam(path, paramIdx), "Filter Cutoff");

    REQUIRE(coord.isLearning(ControlTarget::pluginParam(path, paramIdx)));
    REQUIRE(fix.listener.stateChanges.size() == 1);
    REQUIRE(fix.listener.stateChanges[0].learning == true);

    // Inject CC
    auto msg = juce::MidiMessage::controllerEvent(1, 21, 64);
    ControllerRouter::getInstance().injectMessageForTest("coord_port_1", msg);

    // Coordinator should have fired completion
    REQUIRE(!coord.isLearning(ControlTarget::pluginParam(path, paramIdx)));
    REQUIRE(fix.listener.completions.size() == 1);

    // The binding target should be an AliasRef (alias was found)
    const auto& binding = fix.listener.completions[0].binding;
    REQUIRE(std::holds_alternative<AliasRef>(binding.target));
    auto& aliasRef = std::get<AliasRef>(binding.target);
    REQUIRE(aliasRef.name == "serum.filter_cutoff");
    REQUIRE(aliasRef.pluginType == "serum");

    // Final state change should be learning=false
    REQUIRE(fix.listener.stateChanges.back().learning == false);

    clearAll();
}

TEST_CASE("MidiLearnCoordinator - ControlTarget used when no alias exists",
          "[midi-learn][coordinator]") {
    CoordinatorFixture fix;

    auto c = makeController("coord_port_2");
    ControllerRegistry::getInstance().add(c);

    ChainNodePath path = ChainNodePath::topLevelDevice(11, 110);
    int paramIdx = 3;
    // No alias registered

    MidiLearnCoordinator::getInstance().beginLearn(ControlTarget::pluginParam(path, paramIdx),
                                                   "My Param");

    auto msg = juce::MidiMessage::controllerEvent(1, 7, 100);
    ControllerRouter::getInstance().injectMessageForTest("coord_port_2", msg);

    REQUIRE(fix.listener.completions.size() == 1);
    const auto& binding = fix.listener.completions[0].binding;
    REQUIRE(std::holds_alternative<ControlTarget>(binding.target));
    auto& st = std::get<ControlTarget>(binding.target);
    REQUIRE(st.devicePath == path);
    REQUIRE(st.paramIndex == paramIdx);

    clearAll();
}

TEST_CASE("MidiLearnCoordinator - second beginLearn cancels first", "[midi-learn][coordinator]") {
    CoordinatorFixture fix;

    auto c = makeController("coord_port_3");
    ControllerRegistry::getInstance().add(c);

    ChainNodePath path1 = ChainNodePath::topLevelDevice(20, 200);
    ChainNodePath path2 = ChainNodePath::topLevelDevice(21, 210);

    auto& coord = MidiLearnCoordinator::getInstance();
    coord.beginLearn(ControlTarget::pluginParam(path1, 0), "Param A");
    coord.beginLearn(ControlTarget::pluginParam(path2, 1), "Param B");

    // path1 should have been cancelled: state false for path1
    bool cancelledA = false;
    for (const auto& sc : fix.listener.stateChanges) {
        if (sc.path == path1 && !sc.learning)
            cancelledA = true;
    }
    REQUIRE(cancelledA);
    REQUIRE(coord.isLearning(ControlTarget::pluginParam(path2, 1)));

    coord.cancelLearn();
}

TEST_CASE("MidiLearnCoordinator - macro Learn capture builds DeviceMacro ControlTarget",
          "[midi-learn][coordinator]") {
    CoordinatorFixture fix;

    auto c = makeController("coord_port_macro");
    ControllerRegistry::getInstance().add(c);

    // Track-level macro path — exercises the broader macro coverage.
    ChainNodePath path = ChainNodePath::trackLevel(40);
    int macroIndex = 3;

    // Seed an alias for (path, macroIndex) — Learn must NOT prefer it for macros.
    StoredAlias alias;
    alias.pluginTypeKey = "synth";
    alias.paramIndex = macroIndex;
    alias.path = path;
    AliasRegistry::getInstance().set(AliasLayer::AutoGen, "synth.macro", alias);

    auto& coord = MidiLearnCoordinator::getInstance();
    coord.beginLearn(ControlTarget::deviceMacro(path, macroIndex), "Macro 4");

    REQUIRE(coord.isLearning(ControlTarget::deviceMacro(path, macroIndex)));
    // The plugin-param target must NOT match — kinds are distinct.
    REQUIRE_FALSE(coord.isLearning(ControlTarget::pluginParam(path, macroIndex)));

    auto msg = juce::MidiMessage::controllerEvent(1, 50, 64);
    ControllerRouter::getInstance().injectMessageForTest("coord_port_macro", msg);

    REQUIRE(!coord.isLearning(ControlTarget::deviceMacro(path, macroIndex)));
    REQUIRE(fix.listener.completions.size() == 1);

    const auto& binding = fix.listener.completions[0].binding;
    REQUIRE(std::holds_alternative<ControlTarget>(binding.target));
    const auto& st = std::get<ControlTarget>(binding.target);
    REQUIRE(st.kind == ControlTarget::Kind::DeviceMacro);
    REQUIRE(st.devicePath == path);
    REQUIRE(st.paramIndex == macroIndex);

    // The dot-painting query must agree — this is what flips the macro knob's
    // indicator from green (automap) to orange (Learn'd).
    REQUIRE(BindingRegistry::getInstance().hasActiveStaticBindingForMacro(path, macroIndex));

    clearAll();
}

TEST_CASE("MidiLearnCoordinator - mod-param Learn capture builds ModParam ControlTarget",
          "[midi-learn][coordinator]") {
    CoordinatorFixture fix;

    auto c = makeController("coord_port_modparam");
    ControllerRegistry::getInstance().add(c);

    ChainNodePath path = ChainNodePath::trackLevel(50);
    ModId modId = 7;
    int modParamIndex = 0;

    auto& coord = MidiLearnCoordinator::getInstance();
    coord.beginLearn(ControlTarget::modParam(path, modId, modParamIndex), "LFO 1 Rate");

    REQUIRE(coord.isLearning(ControlTarget::modParam(path, modId, modParamIndex)));
    REQUIRE_FALSE(coord.isLearning(ControlTarget::modParam(path, modId + 1, modParamIndex)));
    REQUIRE_FALSE(coord.isLearning(ControlTarget::pluginParam(path, modParamIndex)));

    auto msg = juce::MidiMessage::controllerEvent(1, 71, 100);
    ControllerRouter::getInstance().injectMessageForTest("coord_port_modparam", msg);

    REQUIRE(!coord.isLearning(ControlTarget::modParam(path, modId, modParamIndex)));
    REQUIRE(fix.listener.completions.size() == 1);

    const auto& binding = fix.listener.completions[0].binding;
    REQUIRE(std::holds_alternative<ControlTarget>(binding.target));
    const auto& st = std::get<ControlTarget>(binding.target);
    REQUIRE(st.kind == ControlTarget::Kind::ModParam);
    REQUIRE(st.devicePath == path);
    REQUIRE(st.modId == modId);
    REQUIRE(st.modParamIndex == modParamIndex);

    clearAll();
}

TEST_CASE("MidiLearnCoordinator - macro Learn does not stage plugin-param listener on same path",
          "[midi-learn][coordinator]") {
    CoordinatorFixture fix;

    // Same path (a 4osc-style device path), same index — but different owner kinds.
    // A macro-Learn at (path, 0, DeviceMacro) must NOT pulse a plugin-param at
    // (path, 0, PluginParam). This is the regression that caused OSC1 Tune to light
    // up when the user clicked Learn on macro 0.
    ChainNodePath path = ChainNodePath::topLevelDevice(70, 700);

    MidiLearnCoordinator::getInstance().beginLearn(ControlTarget::deviceMacro(path, 0), "Macro 1");

    REQUIRE(fix.listener.stateChanges.size() == 1);
    const auto& sc = fix.listener.stateChanges[0];
    REQUIRE(sc.path == path);
    REQUIRE(sc.paramIndex == 0);
    REQUIRE(sc.kind == ControlTarget::Kind::DeviceMacro);
    REQUIRE(sc.learning == true);

    MidiLearnCoordinator::getInstance().cancelLearn();
}

TEST_CASE("MidiLearnCoordinator - clearMappings on a macro leaves automap resolver intact",
          "[midi-learn][coordinator]") {
    CoordinatorFixture fix;

    auto c = makeController("coord_port_clear_macro");
    ControllerRegistry::getInstance().add(c);

    ChainNodePath path = ChainNodePath::topLevelDevice(80, 800);
    int macroIndex = 0;

    auto& reg = BindingRegistry::getInstance();

    // Profile-style automap binding (focused.macro resolver).
    Binding bAuto;
    bAuto.id = juce::Uuid();
    bAuto.source.controllerId = c.id;
    bAuto.source.msgType = BindingMsgType::CC;
    bAuto.source.number = 50;
    ResolverRef rr;
    rr.kind = "focused.macro";
    rr.args.set("macroIndex", juce::String(macroIndex));
    bAuto.target = Target{rr};
    bAuto.mode = BindingMode::Absolute;
    bAuto.range = BindingRange{0.0f, 1.0f, BindingCurve::Linear};
    reg.add(BindingScope::Global, bAuto);

    // User Learn'd Static binding on the same macro.
    Binding bLearn;
    bLearn.id = juce::Uuid();
    bLearn.source.controllerId = c.id;
    bLearn.source.msgType = BindingMsgType::CC;
    bLearn.source.number = 51;
    ControlTarget stLearn;
    stLearn.devicePath = path;
    stLearn.paramIndex = macroIndex;
    stLearn.kind = ControlTarget::Kind::DeviceMacro;
    bLearn.target = Target{stLearn};
    bLearn.mode = BindingMode::Absolute;
    bLearn.range = BindingRange{0.0f, 1.0f, BindingCurve::Linear};
    reg.add(BindingScope::Project, bLearn);

    REQUIRE(reg.hasActiveStaticBindingForMacro(path, macroIndex));

    int removed = MidiLearnCoordinator::getInstance().clearMappings(
        ControlTarget::deviceMacro(path, macroIndex));
    REQUIRE(removed == 1);  // only the Static binding is removed
    REQUIRE_FALSE(reg.hasActiveStaticBindingForMacro(path, macroIndex));

    // Resolver binding survives so the macro falls back to its automap default.
    bool resolverStillThere = false;
    for (const auto& b : reg.bindings(BindingScope::Global)) {
        if (std::holds_alternative<ResolverRef>(b.target)) {
            resolverStillThere = true;
            break;
        }
    }
    REQUIRE(resolverStillThere);

    clearAll();
}

TEST_CASE("MidiLearnCoordinator - clearMappings on a mod-param removes only matching bindings",
          "[midi-learn][coordinator]") {
    CoordinatorFixture fix;

    auto c = makeController("coord_port_modclear");
    ControllerRegistry::getInstance().add(c);

    ChainNodePath path = ChainNodePath::trackLevel(60);
    ModId modId = 3;

    auto makeModParamBinding = [&](int number, ModId mid, int mpIdx) {
        Binding b;
        b.id = juce::Uuid();
        b.source.controllerId = c.id;
        b.source.msgType = BindingMsgType::CC;
        b.source.number = number;
        ControlTarget st;
        st.devicePath = path;
        st.kind = ControlTarget::Kind::ModParam;
        st.modId = mid;
        st.modParamIndex = mpIdx;
        b.target = Target{st};
        b.mode = BindingMode::Absolute;
        b.range = BindingRange{0.0f, 1.0f, BindingCurve::Linear};
        return b;
    };

    auto& reg = BindingRegistry::getInstance();
    reg.add(BindingScope::Project, makeModParamBinding(1, modId, 0));
    reg.add(BindingScope::Project, makeModParamBinding(2, modId, 0));
    reg.add(BindingScope::Project, makeModParamBinding(3, modId + 1, 0));  // different mod

    int removed =
        MidiLearnCoordinator::getInstance().clearMappings(ControlTarget::modParam(path, modId, 0));
    REQUIRE(removed == 2);
    // The unrelated binding remains
    REQUIRE(reg.findFor(ControlTarget::modParam(path, modId + 1, 0)).size() == 1);

    clearAll();
}

TEST_CASE("MidiLearnCoordinator - clearMappings removes bindings and notifies",
          "[midi-learn][coordinator]") {
    CoordinatorFixture fix;

    ChainNodePath path = ChainNodePath::topLevelDevice(30, 300);
    int paramIdx = 2;

    auto c = makeController("coord_port_4");
    ControllerRegistry::getInstance().add(c);

    // Add a binding manually
    auto b = makeStaticBinding(c.id, BindingMsgType::CC, 0, 99, path, paramIdx);
    BindingRegistry::getInstance().add(BindingScope::Project, b);

    int removed = MidiLearnCoordinator::getInstance().clearMappings(
        ControlTarget::pluginParam(path, paramIdx));
    REQUIRE(removed == 1);
    REQUIRE(fix.listener.clears.size() == 1);
    REQUIRE(fix.listener.clears[0].numRemoved == 1);
}

// ============================================================================
// Tests: Config::getMidiLearnDefaultScope round-trip
// ============================================================================

TEST_CASE("Config - midiLearnDefaultScope raw getter/setter round-trip", "[midi-learn][config]") {
    auto& cfg = Config::getInstance();

    // Default is Project (1)
    REQUIRE(cfg.getMidiLearnDefaultScopeRaw() == 1);

    // Set to Global (0) and read back
    cfg.setMidiLearnDefaultScopeRaw(0);
    REQUIRE(cfg.getMidiLearnDefaultScopeRaw() == 0);
    REQUIRE(static_cast<BindingScope>(cfg.getMidiLearnDefaultScopeRaw()) == BindingScope::Global);

    // Restore
    cfg.setMidiLearnDefaultScopeRaw(1);
    REQUIRE(static_cast<BindingScope>(cfg.getMidiLearnDefaultScopeRaw()) == BindingScope::Project);
}
