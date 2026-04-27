#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/controllers/ControllerProfile.hpp"
#include "../magda/daw/core/controllers/ControllerProfileRegistry.hpp"

using namespace magda;

// ============================================================================
// Helpers
// ============================================================================

static ControllerProfile makeTestProfile() {
    ControllerProfile p;
    p.id = "test.profile_1";
    p.vendor = "TestVendor";
    p.name = "Test Profile";

    ControllerProfileControl ctrl1;
    ctrl1.controlId = "knob_1";
    ctrl1.kind = "knob";
    ctrl1.cc = 21;
    ctrl1.channel = 1;
    p.controls.push_back(ctrl1);

    ControllerProfileControl ctrl2;
    ctrl2.controlId = "knob_2";
    ctrl2.kind = "knob";
    ctrl2.cc = 22;
    ctrl2.channel = 1;
    p.controls.push_back(ctrl2);

    ControllerProfileDefaultBinding db1;
    db1.controlId = "knob_1";
    db1.resolverKind = "focused.macro";
    db1.args.set("macroIndex", "0");
    p.defaultBindings.push_back(db1);

    ControllerProfileDefaultBinding db2;
    db2.controlId = "knob_2";
    db2.resolverKind = "focused.macro";
    db2.args.set("macroIndex", "1");
    p.defaultBindings.push_back(db2);

    return p;
}

// ============================================================================
// 1. Encode/decode round-trip
// ============================================================================

TEST_CASE("ControllerProfile - encode/decode round-trip", "[controller_profile]") {
    auto original = makeTestProfile();

    auto encoded = encodeControllerProfile(original);
    auto jsonStr = juce::JSON::toString(encoded);

    auto parsed = juce::JSON::parse(jsonStr);
    auto decoded = decodeControllerProfile(parsed);

    REQUIRE(decoded.has_value());

    CHECK(decoded->id == original.id);
    CHECK(decoded->vendor == original.vendor);
    CHECK(decoded->name == original.name);

    REQUIRE(decoded->controls.size() == original.controls.size());
    CHECK(decoded->controls[0].controlId == original.controls[0].controlId);
    CHECK(decoded->controls[0].kind == original.controls[0].kind);
    CHECK(decoded->controls[0].cc == original.controls[0].cc);
    CHECK(decoded->controls[0].channel == original.controls[0].channel);
    CHECK(decoded->controls[1].controlId == original.controls[1].controlId);
    CHECK(decoded->controls[1].cc == original.controls[1].cc);

    REQUIRE(decoded->defaultBindings.size() == original.defaultBindings.size());
    CHECK(decoded->defaultBindings[0].controlId == original.defaultBindings[0].controlId);
    CHECK(decoded->defaultBindings[0].resolverKind == original.defaultBindings[0].resolverKind);
    CHECK(decoded->defaultBindings[0].args.getValue("macroIndex", "") == "0");
    CHECK(decoded->defaultBindings[1].args.getValue("macroIndex", "") == "1");
}

// ============================================================================
// 2. Decode rejects missing required fields
// ============================================================================

TEST_CASE("ControllerProfile - decode rejects missing id", "[controller_profile]") {
    auto p = makeTestProfile();
    auto encoded = encodeControllerProfile(p);
    encoded.getDynamicObject()->removeProperty("id");
    REQUIRE(!decodeControllerProfile(encoded).has_value());
}

TEST_CASE("ControllerProfile - decode rejects missing name", "[controller_profile]") {
    auto p = makeTestProfile();
    auto encoded = encodeControllerProfile(p);
    encoded.getDynamicObject()->removeProperty("name");
    REQUIRE(!decodeControllerProfile(encoded).has_value());
}

TEST_CASE("ControllerProfile - decode rejects empty controls", "[controller_profile]") {
    auto p = makeTestProfile();
    auto encoded = encodeControllerProfile(p);
    encoded.getDynamicObject()->setProperty("controls", juce::Array<juce::var>{});
    REQUIRE(!decodeControllerProfile(encoded).has_value());
}

// ============================================================================
// 3. Decode skips malformed default bindings
// ============================================================================

TEST_CASE("ControllerProfile - decode skips malformed default bindings", "[controller_profile]") {
    // Build a profile with one valid + one invalid (missing resolverKind) default binding
    auto p = makeTestProfile();
    p.defaultBindings.clear();

    // Valid binding
    ControllerProfileDefaultBinding valid;
    valid.controlId = "knob_1";
    valid.resolverKind = "focused.macro";
    valid.args.set("macroIndex", "0");
    p.defaultBindings.push_back(valid);

    // Encode manually with an invalid extra entry
    auto encoded = encodeControllerProfile(p);
    auto* obj = encoded.getDynamicObject();

    // Inject a malformed binding (missing resolverKind)
    auto bindingsVar = obj->getProperty("defaultBindings");
    auto* arr = bindingsVar.getArray();
    auto* badBinding = new juce::DynamicObject();
    badBinding->setProperty("controlId", "knob_2");
    // intentionally no "resolverKind"
    arr->add(juce::var(badBinding));
    obj->setProperty("defaultBindings", bindingsVar);

    auto decoded = decodeControllerProfile(encoded);
    REQUIRE(decoded.has_value());
    // Only the valid binding survived
    REQUIRE(decoded->defaultBindings.size() == 1);
    CHECK(decoded->defaultBindings[0].controlId == "knob_1");
}

// ============================================================================
// 4. materialiseControllerFromProfile -- basic
// ============================================================================

TEST_CASE("ControllerProfile - materialiseControllerFromProfile basic",
          "[controller_profile][materialise]") {
    auto profile = makeTestProfile();
    auto result = materialiseControllerFromProfile(profile, "midi_port_test", "midi_port_out");

    // Controller fields
    CHECK(!result.controller.id.isNull());
    CHECK(result.controller.name == "Test Profile");
    CHECK(result.controller.vendor == "TestVendor");
    CHECK(result.controller.inputPort == "midi_port_test");
    CHECK(result.controller.outputPort == "midi_port_out");
    CHECK(result.controller.profileId == "test.profile_1");

    // Both knobs have registered resolvers -> 2 bindings
    REQUIRE(result.bindings.size() == 2);

    CHECK(!result.bindings[0].id.isNull());
    CHECK(result.bindings[0].source.controllerId == result.controller.id);
    CHECK(result.bindings[0].source.msgType == BindingMsgType::CC);
    CHECK(result.bindings[0].source.channel == 1);
    CHECK(result.bindings[0].source.number == 21);  // knob_1 CC
    CHECK(result.bindings[0].mode == BindingMode::Absolute);

    // Target should be a ResolverRef
    REQUIRE(std::holds_alternative<ResolverRef>(result.bindings[0].target));
    auto ref0 = std::get<ResolverRef>(result.bindings[0].target);
    CHECK(ref0.kind == "focused.macro");
    CHECK(ref0.args.getValue("macroIndex", "") == "0");

    CHECK(result.bindings[1].source.number == 22);  // knob_2 CC
    auto ref1 = std::get<ResolverRef>(result.bindings[1].target);
    CHECK(ref1.args.getValue("macroIndex", "") == "1");
}

// ============================================================================
// 5. materialiseControllerFromProfile skips unregistered resolver kinds
// ============================================================================

TEST_CASE("ControllerProfile - materialiseControllerFromProfile skips unknown resolver",
          "[controller_profile][materialise]") {
    auto profile = makeTestProfile();

    // Replace second binding with an unregistered resolver kind
    profile.defaultBindings[1].resolverKind = "nonexistent.resolver_xyz";

    auto result = materialiseControllerFromProfile(profile, "midi_port_test");

    // Only the first binding (with registered resolver) should be produced
    REQUIRE(result.bindings.size() == 1);
    auto ref = std::get<ResolverRef>(result.bindings[0].target);
    CHECK(ref.kind == "focused.macro");
}

// ============================================================================
// 6. materialiseControllerFromProfile skips unknown controlId
// ============================================================================

TEST_CASE("ControllerProfile - materialiseControllerFromProfile skips unknown controlId",
          "[controller_profile][materialise]") {
    auto profile = makeTestProfile();

    // Replace second binding's controlId with one that doesn't exist in controls
    profile.defaultBindings[1].controlId = "knob_nonexistent_99";

    auto result = materialiseControllerFromProfile(profile, "midi_port_test");

    // Only the first binding should be produced
    REQUIRE(result.bindings.size() == 1);
    CHECK(result.bindings[0].source.number == 21);  // knob_1
}
