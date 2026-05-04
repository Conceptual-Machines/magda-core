#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/audio/plugins/FaustMetadataParser.hpp"

using namespace magda::daw::audio;

// ============================================================================
// parseFaustLabel — clean label
// ============================================================================

TEST_CASE("parseFaustLabel - empty input", "[faust][metadata]") {
    auto p = parseFaustLabel("");
    REQUIRE(p.cleanLabel == "");
    REQUIRE(p.metadata.slotIndex == -1);
}

TEST_CASE("parseFaustLabel - no annotations", "[faust][metadata]") {
    auto p = parseFaustLabel("Cutoff");
    REQUIRE(p.cleanLabel == "Cutoff");
    REQUIRE(p.metadata.unit.isEmpty());
}

TEST_CASE("parseFaustLabel - strips known annotations", "[faust][metadata]") {
    auto p = parseFaustLabel("Cutoff [unit:Hz] [scale:log] [idx:7]");
    REQUIRE(p.cleanLabel == "Cutoff");
    REQUIRE(p.metadata.unit == "Hz");
    REQUIRE(p.metadata.logScale);
    REQUIRE(p.metadata.slotIndex == 7);
}

TEST_CASE("parseFaustLabel - keeps unknown annotations intact", "[faust][metadata]") {
    auto p = parseFaustLabel("Cutoff [tooltip:nice] [unit:Hz]");
    REQUIRE(p.cleanLabel == "Cutoff [tooltip:nice]");
    REQUIRE(p.metadata.unit == "Hz");
}

TEST_CASE("parseFaustLabel - collapses whitespace after stripping", "[faust][metadata]") {
    auto p = parseFaustLabel("  Mode  [unit:Hz]   [idx:3]   ");
    REQUIRE(p.cleanLabel == "Mode");
    REQUIRE(p.metadata.unit == "Hz");
    REQUIRE(p.metadata.slotIndex == 3);
}

TEST_CASE("parseFaustLabel - unterminated bracket left verbatim", "[faust][metadata]") {
    auto p = parseFaustLabel("Cutoff [unit:Hz");
    REQUIRE(p.cleanLabel == "Cutoff [unit:Hz");
}

// ============================================================================
// parseFaustLabel — slot index
// ============================================================================

TEST_CASE("parseFaustLabel - idx zero", "[faust][metadata]") {
    auto p = parseFaustLabel("X [idx:0]");
    REQUIRE(p.metadata.slotIndex == 0);
}

TEST_CASE("parseFaustLabel - idx negative kept (pool decides policy)", "[faust][metadata]") {
    auto p = parseFaustLabel("X [idx:-3]");
    REQUIRE(p.metadata.slotIndex == -3);
}

TEST_CASE("parseFaustLabel - idx out-of-range kept (pool decides policy)", "[faust][metadata]") {
    auto p = parseFaustLabel("X [idx:99]");
    REQUIRE(p.metadata.slotIndex == 99);
}

// ============================================================================
// parseFaustLabel — scale
// ============================================================================

TEST_CASE("parseFaustLabel - scale log", "[faust][metadata]") {
    auto p = parseFaustLabel("Freq [scale:log]");
    REQUIRE(p.metadata.logScale);
}

TEST_CASE("parseFaustLabel - scale lin does not set log", "[faust][metadata]") {
    auto p = parseFaustLabel("Freq [scale:lin]");
    REQUIRE_FALSE(p.metadata.logScale);
}

TEST_CASE("parseFaustLabel - scale exp does not set log", "[faust][metadata]") {
    auto p = parseFaustLabel("Curve [scale:exp]");
    REQUIRE_FALSE(p.metadata.logScale);
}

// ============================================================================
// parseFaustLabel — style menu / radio
// ============================================================================

TEST_CASE("parseFaustLabel - style menu populates choices", "[faust][metadata]") {
    auto p = parseFaustLabel("Mode [style:menu{'Off':0;'Low':1;'High':2}]");
    REQUIRE(p.cleanLabel == "Mode");
    REQUIRE(p.metadata.isMenuStyle);
    REQUIRE(p.metadata.menuChoices.size() == 3);
    REQUIRE(p.metadata.menuChoices[0].first == 0.0f);
    REQUIRE(p.metadata.menuChoices[0].second == "Off");
    REQUIRE(p.metadata.menuChoices[1].first == 1.0f);
    REQUIRE(p.metadata.menuChoices[1].second == "Low");
    REQUIRE(p.metadata.menuChoices[2].first == 2.0f);
    REQUIRE(p.metadata.menuChoices[2].second == "High");
}

TEST_CASE("parseFaustLabel - style radio populates choices", "[faust][metadata]") {
    auto p = parseFaustLabel("Voice [style:radio{'Mono':0;'Poly':1}]");
    REQUIRE(p.metadata.isMenuStyle);
    REQUIRE(p.metadata.menuChoices.size() == 2);
    REQUIRE(p.metadata.menuChoices[1].second == "Poly");
}

TEST_CASE("parseFaustLabel - style knob recognised but no menu", "[faust][metadata]") {
    auto p = parseFaustLabel("Cutoff [style:knob]");
    REQUIRE(p.cleanLabel == "Cutoff");
    REQUIRE_FALSE(p.metadata.isMenuStyle);
}

TEST_CASE("parseFaustLabel - menu accepts double quotes too", "[faust][metadata]") {
    auto p = parseFaustLabel("Mode [style:menu{\"Off\":0;\"On\":1}]");
    REQUIRE(p.metadata.menuChoices.size() == 2);
    REQUIRE(p.metadata.menuChoices[0].second == "Off");
}

TEST_CASE("parseFaustLabel - menu skips malformed entries", "[faust][metadata]") {
    auto p = parseFaustLabel("Mode [style:menu{'Off':0;invalid;'High':2}]");
    REQUIRE(p.metadata.menuChoices.size() == 2);
    REQUIRE(p.metadata.menuChoices[1].second == "High");
}

TEST_CASE("parseFaustLabel - empty menu body yields empty choices", "[faust][metadata]") {
    auto p = parseFaustLabel("Mode [style:menu{}]");
    REQUIRE(p.metadata.isMenuStyle);
    REQUIRE(p.metadata.menuChoices.empty());
}

// ============================================================================
// applyFaustAnnotation — direct API
// ============================================================================

TEST_CASE("applyFaustAnnotation - rejects unknown key", "[faust][metadata]") {
    ControlMetadata m;
    REQUIRE_FALSE(applyFaustAnnotation("tooltip", "hi", m));
    REQUIRE(m.unit.isEmpty());
}

TEST_CASE("applyFaustAnnotation - case-insensitive key handled by caller", "[faust][metadata]") {
    // applyFaustAnnotation expects keys lowercased; parseFaustLabel does that.
    // Direct-call check confirms behaviour without that pre-step.
    ControlMetadata m;
    REQUIRE(applyFaustAnnotation("unit", "dB", m));
    REQUIRE(m.unit == "dB");
}

// ============================================================================
// mergeFaustMetadata — group-level vs control-level
// ============================================================================

TEST_CASE("mergeFaustMetadata - control wins on conflict", "[faust][metadata]") {
    ControlMetadata group;
    group.unit = "Hz";
    group.logScale = true;

    ControlMetadata control;
    control.unit = "dB";

    mergeFaustMetadata(group, control);
    REQUIRE(group.unit == "dB");      // control overrode
    REQUIRE(group.logScale == true);  // group survived (control didn't set it)
}

TEST_CASE("mergeFaustMetadata - empty control leaves group intact", "[faust][metadata]") {
    ControlMetadata group;
    group.unit = "Hz";
    group.slotIndex = 5;

    ControlMetadata control;
    mergeFaustMetadata(group, control);
    REQUIRE(group.unit == "Hz");
    REQUIRE(group.slotIndex == 5);
}

TEST_CASE("mergeFaustMetadata - control idx overrides group idx", "[faust][metadata]") {
    ControlMetadata group;
    group.slotIndex = 1;

    ControlMetadata control;
    control.slotIndex = 7;

    mergeFaustMetadata(group, control);
    REQUIRE(group.slotIndex == 7);
}

TEST_CASE("mergeFaustMetadata - control menu replaces group menu", "[faust][metadata]") {
    ControlMetadata group;
    group.isMenuStyle = true;
    group.menuChoices = {{0.0f, "GroupA"}, {1.0f, "GroupB"}};

    ControlMetadata control;
    control.isMenuStyle = true;
    control.menuChoices = {{0.0f, "CtrlA"}};

    mergeFaustMetadata(group, control);
    REQUIRE(group.menuChoices.size() == 1);
    REQUIRE(group.menuChoices[0].second == "CtrlA");
}

// ============================================================================
// parseMenuChoices — direct
// ============================================================================

TEST_CASE("parseMenuChoices - basic", "[faust][metadata]") {
    auto c = parseMenuChoices("'A':0;'B':1");
    REQUIRE(c.size() == 2);
    REQUIRE(c[0].first == 0.0f);
    REQUIRE(c[0].second == "A");
}

TEST_CASE("parseMenuChoices - float values", "[faust][metadata]") {
    auto c = parseMenuChoices("'Half':0.5;'Full':1.0");
    REQUIRE(c.size() == 2);
    REQUIRE(c[0].first == 0.5f);
}

TEST_CASE("parseMenuChoices - whitespace tolerated", "[faust][metadata]") {
    auto c = parseMenuChoices("  'A' : 0 ;  'B' : 1  ");
    REQUIRE(c.size() == 2);
    REQUIRE(c[1].second == "B");
}
