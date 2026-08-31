#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/controllers/ControllerRegistry.hpp"

using namespace magda;

// ============================================================================
// Helpers
// ============================================================================

static Controller makeController(const juce::String& name, const juce::String& inputPort) {
    Controller c;
    c.id = juce::Uuid();
    c.name = name;
    c.vendor = "TestVendor";
    c.inputPort = inputPort;
    return c;
}

static void clearRegistry() {
    auto& reg = ControllerRegistry::getInstance();
    for (const auto& c : reg.all())
        reg.remove(c.id);
}

// ============================================================================
// Basic CRUD
// ============================================================================

TEST_CASE("ControllerRegistry - add and find", "[controllers][registry]") {
    clearRegistry();
    auto& reg = ControllerRegistry::getInstance();

    auto c = makeController("Akai MPK", "midi_in_1");
    reg.add(c);

    auto found = reg.find(c.id);
    REQUIRE(found.has_value());
    REQUIRE(found->name == "Akai MPK");
    REQUIRE(found->inputPort == "midi_in_1");
}

TEST_CASE("ControllerRegistry - add is idempotent (update on duplicate id)",
          "[controllers][registry]") {
    clearRegistry();
    auto& reg = ControllerRegistry::getInstance();

    auto c = makeController("Akai", "midi_in_1");
    reg.add(c);
    c.name = "Akai MPK";
    reg.add(c);

    REQUIRE(reg.all().size() == 1);
    REQUIRE(reg.find(c.id)->name == "Akai MPK");
}

TEST_CASE("ControllerRegistry - remove", "[controllers][registry]") {
    clearRegistry();
    auto& reg = ControllerRegistry::getInstance();

    auto c = makeController("Akai", "midi_in_1");
    reg.add(c);
    REQUIRE(reg.all().size() == 1);

    reg.remove(c.id);
    REQUIRE(reg.all().empty());
    REQUIRE(!reg.find(c.id).has_value());
}

// ============================================================================
// findByInputPort
// ============================================================================

TEST_CASE("ControllerRegistry - findByInputPort with multiple controllers",
          "[controllers][registry]") {
    clearRegistry();
    auto& reg = ControllerRegistry::getInstance();

    auto c1 = makeController("Akai", "midi_port_A");
    auto c2 = makeController("Korg", "midi_port_B");
    auto c3 = makeController("Novation", "midi_port_A");  // same port as c1

    reg.add(c1);
    reg.add(c2);
    reg.add(c3);

    // findByInputPort returns the first match
    auto found = reg.findByInputPort("midi_port_B");
    REQUIRE(found.has_value());
    REQUIRE(found->name == "Korg");

    // Unknown port returns nullopt
    REQUIRE(!reg.findByInputPort("nonexistent_port").has_value());
}

// ============================================================================
// isControllerInputPort (MIDI thread safe)
// ============================================================================

TEST_CASE("ControllerRegistry - isControllerInputPort", "[controllers][registry]") {
    clearRegistry();
    auto& reg = ControllerRegistry::getInstance();

    auto c = makeController("Akai", "midi_port_controller");
    reg.add(c);

    REQUIRE(reg.isControllerInputPort("midi_port_controller"));
    REQUIRE(!reg.isControllerInputPort("some_other_port"));
}

// ============================================================================
// JSON round-trip
// ============================================================================

TEST_CASE("ControllerRegistry - JSON round-trip via loadFromConfig/saveToConfig",
          "[controllers][registry]") {
    clearRegistry();
    auto& reg = ControllerRegistry::getInstance();

    auto c1 = makeController("Akai MPK", "midi_akai");
    auto c2 = makeController("Korg nano", "midi_korg");
    c2.vendor = "Korg Corp";
    c2.outputPort = "midi_korg_out";

    reg.add(c1);
    reg.add(c2);

    auto json = reg.saveToConfig();

    clearRegistry();
    reg.loadFromConfig(json);

    REQUIRE(reg.all().size() == 2);

    auto f1 = reg.find(c1.id);
    REQUIRE(f1.has_value());
    REQUIRE(f1->name == "Akai MPK");
    REQUIRE(f1->inputPort == "midi_akai");
    REQUIRE(f1->id == c1.id);  // UUID round-trip

    auto f2 = reg.find(c2.id);
    REQUIRE(f2.has_value());
    REQUIRE(f2->vendor == "Korg Corp");
    REQUIRE(f2->outputPort == "midi_korg_out");
}
