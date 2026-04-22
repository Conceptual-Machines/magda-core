#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/aliases/Target.hpp"
#include "../magda/daw/core/controllers/BindingRegistry.hpp"

using namespace magda;

// ============================================================================
// Helpers
// ============================================================================

static Binding makeBinding(const ControllerId& controllerId, BindingMsgType msgType, int channel,
                           int number) {
    Binding b;
    b.id = juce::Uuid();
    b.source.controllerId = controllerId;
    b.source.msgType = msgType;
    b.source.channel = channel;
    b.source.number = number;
    // Use a simple StaticTarget
    StaticTarget st;
    st.devicePath = ChainNodePath::topLevelDevice(1, 10);
    st.paramIndex = 0;
    b.target = Target{st};
    b.mode = BindingMode::Absolute;
    return b;
}

static void clearRegistry() {
    auto& reg = BindingRegistry::getInstance();
    for (const auto& b : reg.bindings(BindingScope::Global))
        reg.remove(BindingScope::Global, b.id);
    reg.clearProject();
}

// ============================================================================
// Basic CRUD
// ============================================================================

TEST_CASE("BindingRegistry - add and bindings()", "[controllers][binding_registry]") {
    clearRegistry();
    auto& reg = BindingRegistry::getInstance();

    ControllerId cid = juce::Uuid();
    auto b = makeBinding(cid, BindingMsgType::CC, 1, 7);
    reg.add(BindingScope::Global, b);

    auto all = reg.bindings(BindingScope::Global);
    REQUIRE(all.size() == 1);
    REQUIRE(all[0].id == b.id);
}

TEST_CASE("BindingRegistry - add is idempotent (update on duplicate id)",
          "[controllers][binding_registry]") {
    clearRegistry();
    auto& reg = BindingRegistry::getInstance();

    ControllerId cid = juce::Uuid();
    auto b = makeBinding(cid, BindingMsgType::CC, 1, 7);
    reg.add(BindingScope::Global, b);
    b.source.number = 99;
    reg.add(BindingScope::Global, b);

    REQUIRE(reg.bindings(BindingScope::Global).size() == 1);
    REQUIRE(reg.bindings(BindingScope::Global)[0].source.number == 99);
}

TEST_CASE("BindingRegistry - remove", "[controllers][binding_registry]") {
    clearRegistry();
    auto& reg = BindingRegistry::getInstance();

    ControllerId cid = juce::Uuid();
    auto b = makeBinding(cid, BindingMsgType::CC, 1, 7);
    reg.add(BindingScope::Project, b);
    REQUIRE(reg.bindings(BindingScope::Project).size() == 1);

    reg.remove(BindingScope::Project, b.id);
    REQUIRE(reg.bindings(BindingScope::Project).empty());
}

TEST_CASE("BindingRegistry - clearProject", "[controllers][binding_registry]") {
    clearRegistry();
    auto& reg = BindingRegistry::getInstance();

    ControllerId cid = juce::Uuid();
    reg.add(BindingScope::Project, makeBinding(cid, BindingMsgType::CC, 1, 7));
    reg.add(BindingScope::Project, makeBinding(cid, BindingMsgType::CC, 1, 8));
    REQUIRE(reg.bindings(BindingScope::Project).size() == 2);

    reg.clearProject();
    REQUIRE(reg.bindings(BindingScope::Project).empty());
}

// ============================================================================
// findForSource - channel matching
// ============================================================================

TEST_CASE("BindingRegistry - findForSource exact channel match",
          "[controllers][binding_registry]") {
    clearRegistry();
    auto& reg = BindingRegistry::getInstance();

    ControllerId cid = juce::Uuid();
    // channel=1 binding
    auto b1 = makeBinding(cid, BindingMsgType::CC, 1, 74);
    // channel=2 binding
    auto b2 = makeBinding(cid, BindingMsgType::CC, 2, 74);
    reg.add(BindingScope::Global, b1);
    reg.add(BindingScope::Global, b2);

    auto found = reg.findForSource(cid, BindingMsgType::CC, 1, 74);
    REQUIRE(found.size() == 1);
    REQUIRE(found[0].source.channel == 1);
}

TEST_CASE("BindingRegistry - findForSource channel wildcard (channel=0)",
          "[controllers][binding_registry]") {
    clearRegistry();
    auto& reg = BindingRegistry::getInstance();

    ControllerId cid = juce::Uuid();
    // channel=0 means "any channel"
    auto b = makeBinding(cid, BindingMsgType::CC, 0, 74);
    reg.add(BindingScope::Global, b);

    // Should match for channel 1
    auto found1 = reg.findForSource(cid, BindingMsgType::CC, 1, 74);
    REQUIRE(found1.size() == 1);

    // Should match for channel 15
    auto found15 = reg.findForSource(cid, BindingMsgType::CC, 15, 74);
    REQUIRE(found15.size() == 1);
}

TEST_CASE("BindingRegistry - findForSource both global and project scopes",
          "[controllers][binding_registry]") {
    clearRegistry();
    auto& reg = BindingRegistry::getInstance();

    ControllerId cid = juce::Uuid();
    auto bg = makeBinding(cid, BindingMsgType::CC, 0, 74);
    auto bp = makeBinding(cid, BindingMsgType::CC, 0, 74);
    reg.add(BindingScope::Global, bg);
    reg.add(BindingScope::Project, bp);

    // Both should appear in the snapshot
    auto found = reg.findForSource(cid, BindingMsgType::CC, 1, 74);
    REQUIRE(found.size() == 2);
}

TEST_CASE("BindingRegistry - findForSource unknown CC returns empty",
          "[controllers][binding_registry]") {
    clearRegistry();
    auto& reg = BindingRegistry::getInstance();

    ControllerId cid = juce::Uuid();
    auto b = makeBinding(cid, BindingMsgType::CC, 0, 74);
    reg.add(BindingScope::Global, b);

    auto found = reg.findForSource(cid, BindingMsgType::CC, 1, 99);  // different number
    REQUIRE(found.empty());
}

// ============================================================================
// Orphan policy: bindings referencing a deleted controller survive round-trip
// ============================================================================

TEST_CASE("BindingRegistry - orphan binding survives round-trip",
          "[controllers][binding_registry]") {
    clearRegistry();
    auto& reg = BindingRegistry::getInstance();

    // Create a binding with an arbitrary (potentially "orphan") controller ID
    ControllerId orphanId = juce::Uuid();
    auto b = makeBinding(orphanId, BindingMsgType::CC, 1, 7);
    reg.add(BindingScope::Global, b);

    auto json = reg.saveGlobal();

    clearRegistry();
    reg.loadGlobal(json);

    auto all = reg.bindings(BindingScope::Global);
    REQUIRE(all.size() == 1);
    REQUIRE(all[0].source.controllerId == orphanId);
}

// ============================================================================
// JSON round-trip
// ============================================================================

TEST_CASE("BindingRegistry - JSON round-trip for project bindings",
          "[controllers][binding_registry]") {
    clearRegistry();
    auto& reg = BindingRegistry::getInstance();

    ControllerId cid = juce::Uuid();
    auto b1 = makeBinding(cid, BindingMsgType::CC, 1, 7);
    auto b2 = makeBinding(cid, BindingMsgType::Note, 2, 60);
    auto b3 = makeBinding(cid, BindingMsgType::PitchBend, 0, 0);
    b1.mode = BindingMode::Toggle;
    b2.range = BindingRange{0.1f, 0.9f, BindingCurve::Log};

    reg.add(BindingScope::Project, b1);
    reg.add(BindingScope::Project, b2);
    reg.add(BindingScope::Project, b3);

    auto json = reg.saveProject();
    reg.clearProject();
    reg.loadProject(json);

    auto all = reg.bindings(BindingScope::Project);
    REQUIRE(all.size() == 3);

    // Find b1 by id and verify mode
    bool foundB1 = false;
    for (const auto& b : all) {
        if (b.id == b1.id) {
            REQUIRE(b.mode == BindingMode::Toggle);
            foundB1 = true;
        }
        if (b.id == b2.id) {
            REQUIRE(b.range.min == Catch::Approx(0.1f));
            REQUIRE(b.range.max == Catch::Approx(0.9f));
            REQUIRE(b.range.curve == BindingCurve::Log);
        }
    }
    REQUIRE(foundB1);
}
