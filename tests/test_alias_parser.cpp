#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/aliases/ParamSigilParser.hpp"
#include "../magda/daw/core/aliases/Target.hpp"

using namespace magda;

// ============================================================================
// isSigilToken
// ============================================================================

TEST_CASE("isSigilToken - valid @ sigil tokens", "[aliases][parser]") {
    REQUIRE(isSigilToken("@serum.filter_1"));
    REQUIRE(isSigilToken("@focused.macro_1"));
    REQUIRE(isSigilToken("@master.pan"));
    REQUIRE(isSigilToken("@selected.volume"));
}

TEST_CASE("isSigilToken - invalid tokens", "[aliases][parser]") {
    REQUIRE_FALSE(isSigilToken(""));
    REQUIRE_FALSE(isSigilToken("serum.filter"));     // no sigil
    REQUIRE_FALSE(isSigilToken("@serum"));           // no dot
    REQUIRE_FALSE(isSigilToken("#serum.cutoff"));    // # is not accepted
    REQUIRE_FALSE(isSigilToken("$mysynth.volume"));  // $ is not accepted
    REQUIRE_FALSE(isSigilToken("plain_string"));
}

// ============================================================================
// tryParse -- '@' sigil
// ============================================================================

TEST_CASE("tryParse - @ basic", "[aliases][parser]") {
    auto result = tryParse("@serum.filter_1");
    REQUIRE(result.has_value());
    REQUIRE(result->pluginKey == "serum");
    REQUIRE(result->paramKey == "filter_1");
    REQUIRE_FALSE(result->isScoped);
}

TEST_CASE("tryParse - @ scoped: focused", "[aliases][parser]") {
    auto result = tryParse("@focused.macro_1");
    REQUIRE(result.has_value());
    REQUIRE(result->pluginKey == "focused");
    REQUIRE(result->paramKey == "macro_1");
    REQUIRE(result->isScoped);
}

TEST_CASE("tryParse - @ scoped: selected", "[aliases][parser]") {
    auto result = tryParse("@selected.volume");
    REQUIRE(result.has_value());
    REQUIRE(result->isScoped);
    REQUIRE(result->pluginKey == "selected");
    REQUIRE(result->paramKey == "volume");
}

TEST_CASE("tryParse - @ scoped: master", "[aliases][parser]") {
    auto result = tryParse("@master.pan");
    REQUIRE(result.has_value());
    REQUIRE(result->isScoped);
    REQUIRE(result->pluginKey == "master");
    REQUIRE(result->paramKey == "pan");
}

// ============================================================================
// tryParse -- '#' and '$' are rejected as malformed
// ============================================================================

TEST_CASE("tryParse - # is rejected as malformed", "[aliases][parser]") {
    REQUIRE_FALSE(tryParse("#serum.cutoff").has_value());
    REQUIRE_FALSE(tryParse("#serum_1.filter_1").has_value());
    REQUIRE_FALSE(tryParse("#serum_2.cutoff").has_value());
}

TEST_CASE("tryParse - $ is rejected as malformed", "[aliases][parser]") {
    REQUIRE_FALSE(tryParse("$mysynth.filter_1").has_value());
    REQUIRE_FALSE(tryParse("$var.param").has_value());
}

// ============================================================================
// tryParse -- malformed inputs
// ============================================================================

TEST_CASE("tryParse - empty string", "[aliases][parser]") {
    REQUIRE_FALSE(tryParse("").has_value());
}

TEST_CASE("tryParse - no sigil character", "[aliases][parser]") {
    REQUIRE_FALSE(tryParse("serum.cutoff").has_value());
}

TEST_CASE("tryParse - dot at position 1 (empty plugin key)", "[aliases][parser]") {
    REQUIRE_FALSE(tryParse("@.cutoff").has_value());
}

TEST_CASE("tryParse - no dot (no param key)", "[aliases][parser]") {
    REQUIRE_FALSE(tryParse("@serum").has_value());
}

TEST_CASE("tryParse - dot at end (empty param key)", "[aliases][parser]") {
    REQUIRE_FALSE(tryParse("@serum.").has_value());
}

// ============================================================================
// Target encode/decode round-trip
// ============================================================================

TEST_CASE("Target encodeTarget/decodeTarget - StaticTarget round-trip", "[aliases][target]") {
    StaticTarget st;
    st.devicePath = ChainNodePath::chainDevice(1, 10, 20, 30);
    st.paramIndex = 7;

    Target t{st};
    auto encoded = encodeTarget(t);
    auto decoded = decodeTarget(encoded);

    REQUIRE(decoded.has_value());
    REQUIRE(std::holds_alternative<StaticTarget>(*decoded));

    const auto& dSt = std::get<StaticTarget>(*decoded);
    REQUIRE(dSt.paramIndex == 7);
    REQUIRE(dSt.devicePath == st.devicePath);
}

TEST_CASE("Target encodeTarget/decodeTarget - StaticTarget DeviceMacro round-trip",
          "[aliases][target]") {
    StaticTarget st;
    st.devicePath = ChainNodePath::trackLevel(1);
    st.paramIndex = 3;
    st.owner = StaticTarget::Owner::DeviceMacro;

    Target t{st};
    auto decoded = decodeTarget(encodeTarget(t));

    REQUIRE(decoded.has_value());
    REQUIRE(std::holds_alternative<StaticTarget>(*decoded));
    const auto& dSt = std::get<StaticTarget>(*decoded);
    REQUIRE(dSt.owner == StaticTarget::Owner::DeviceMacro);
    REQUIRE(dSt.paramIndex == 3);
    REQUIRE(dSt.devicePath == st.devicePath);
}

TEST_CASE("Target encodeTarget/decodeTarget - StaticTarget ModParam round-trip",
          "[aliases][target]") {
    StaticTarget st;
    st.devicePath = ChainNodePath::trackLevel(2);
    st.owner = StaticTarget::Owner::ModParam;
    st.modId = 42;
    st.modParamIndex = 0;

    Target t{st};
    auto decoded = decodeTarget(encodeTarget(t));

    REQUIRE(decoded.has_value());
    REQUIRE(std::holds_alternative<StaticTarget>(*decoded));
    const auto& dSt = std::get<StaticTarget>(*decoded);
    REQUIRE(dSt.owner == StaticTarget::Owner::ModParam);
    REQUIRE(dSt.modId == 42);
    REQUIRE(dSt.modParamIndex == 0);
    REQUIRE(dSt.devicePath == st.devicePath);
}

TEST_CASE("StaticTarget - isValid honors owner kind", "[aliases][target]") {
    StaticTarget plug;
    plug.devicePath = ChainNodePath::trackLevel(1);
    plug.paramIndex = 0;
    REQUIRE(plug.isValid());

    StaticTarget mp;
    mp.devicePath = ChainNodePath::trackLevel(1);
    mp.owner = StaticTarget::Owner::ModParam;
    REQUIRE_FALSE(mp.isValid());  // modId / modParamIndex unset
    mp.modId = 5;
    mp.modParamIndex = 0;
    REQUIRE(mp.isValid());

    // ModParam with mismatched fields compares unequal even if path & owner match
    StaticTarget mp2 = mp;
    mp2.modId = 6;
    REQUIRE(mp != mp2);
}

TEST_CASE("Target encodeTarget/decodeTarget - AliasRef round-trip", "[aliases][target]") {
    AliasRef ar;
    ar.name = "filter_cutoff";
    ar.pluginType = "serum";

    Target t{ar};
    auto encoded = encodeTarget(t);
    auto decoded = decodeTarget(encoded);

    REQUIRE(decoded.has_value());
    REQUIRE(std::holds_alternative<AliasRef>(*decoded));

    const auto& dAr = std::get<AliasRef>(*decoded);
    REQUIRE(dAr.name == "filter_cutoff");
    REQUIRE(dAr.pluginType == "serum");
}

TEST_CASE("Target encodeTarget/decodeTarget - ResolverRef round-trip", "[aliases][target]") {
    ResolverRef rr;
    rr.kind = "focused.macro";
    rr.args.set("macroIndex", "2");

    Target t{rr};
    auto encoded = encodeTarget(t);
    auto decoded = decodeTarget(encoded);

    REQUIRE(decoded.has_value());
    REQUIRE(std::holds_alternative<ResolverRef>(*decoded));

    const auto& dRr = std::get<ResolverRef>(*decoded);
    REQUIRE(dRr.kind == "focused.macro");
    REQUIRE(dRr.args.getValue("macroIndex", "") == "2");
}

TEST_CASE("Target decodeTarget - malformed JSON returns nullopt", "[aliases][target]") {
    REQUIRE_FALSE(decodeTarget("not json").has_value());
    REQUIRE_FALSE(decodeTarget("{}").has_value());  // no "kind" field -> unknown kind
    REQUIRE_FALSE(decodeTarget("").has_value());
}

TEST_CASE("toDebugString - returns non-empty string for all variants", "[aliases][target]") {
    Target st{StaticTarget{}};
    REQUIRE(toDebugString(st).isNotEmpty());

    Target ar{AliasRef{"filter_cutoff", "serum"}};
    REQUIRE(toDebugString(ar).isNotEmpty());
    REQUIRE(toDebugString(ar).contains("filter_cutoff"));

    Target rr{ResolverRef{"focused.macro", {}}};
    REQUIRE(toDebugString(rr).isNotEmpty());
    REQUIRE(toDebugString(rr).contains("focused.macro"));
}
