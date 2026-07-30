#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/audio/plugins/FaustParamInfo.hpp"

using namespace magda::daw::audio;
using magda::DisplayFormat;
using magda::ParameterScale;

namespace {

FaustParamSlot makeContinuousSlot(int index, const juce::String& label, float min = 0.0f,
                                  float max = 1.0f, bool logScale = false) {
    FaustParamSlot s;
    s.index = index;
    s.active = true;
    s.label = label;
    s.kind = FaustParamSlot::Kind::Continuous;
    s.minValue = min;
    s.maxValue = max;
    s.stepValue = (max - min) / 100.0f;
    s.defaultValue = min;
    s.logScale = logScale;
    return s;
}

FaustParamSlot makeBooleanSlot(int index, const juce::String& label) {
    FaustParamSlot s;
    s.index = index;
    s.active = true;
    s.label = label;
    s.kind = FaustParamSlot::Kind::Boolean;
    s.minValue = 0.0f;
    s.maxValue = 1.0f;
    s.stepValue = 1.0f;
    s.defaultValue = 0.0f;
    return s;
}

FaustParamSlot makeTriggerSlot(int index, const juce::String& label) {
    auto s = makeBooleanSlot(index, label);
    s.kind = FaustParamSlot::Kind::Trigger;
    return s;
}

FaustParamSlot makeDiscreteSlot(int index, const juce::String& label,
                                std::vector<std::pair<float, juce::String>> choices,
                                float defaultValue = 0.0f) {
    FaustParamSlot s;
    s.index = index;
    s.active = true;
    s.label = label;
    s.kind = FaustParamSlot::Kind::Discrete;
    s.minValue = 0.0f;
    s.maxValue = static_cast<float>(choices.size() - 1);
    s.stepValue = 1.0f;
    s.defaultValue = defaultValue;
    s.choices = std::move(choices);
    return s;
}

}  // namespace

// ============================================================================
// Inactive slot → placeholder
// ============================================================================

TEST_CASE("paramInfoFromSlot - inactive slot returns placeholder", "[faust][paraminfo]") {
    FaustParamSlot s;
    s.index = 7;
    s.active = false;

    auto info = paramInfoFromSlot(s);
    REQUIRE(info.paramIndex == 7);
    REQUIRE_FALSE(info.modulatable);
    REQUIRE(info.scale == ParameterScale::Linear);
    REQUIRE(info.name.contains("8"));  // human-friendly index in the placeholder name
}

// ============================================================================
// Continuous
// ============================================================================

TEST_CASE("paramInfoFromSlot - continuous linear", "[faust][paraminfo]") {
    auto info = paramInfoFromSlot(makeContinuousSlot(0, "Drive", 0.0f, 10.0f));
    REQUIRE(info.scale == ParameterScale::Linear);
    REQUIRE(info.name == "Drive");
    REQUIRE(info.minValue == 0.0f);
    REQUIRE(info.maxValue == 10.0f);
}

TEST_CASE("paramInfoFromSlot - continuous log", "[faust][paraminfo]") {
    auto info = paramInfoFromSlot(makeContinuousSlot(0, "Cutoff", 20.0f, 20000.0f, true));
    REQUIRE(info.scale == ParameterScale::Logarithmic);
}

TEST_CASE("paramInfoFromSlot - continuous unit propagates", "[faust][paraminfo]") {
    auto slot = makeContinuousSlot(0, "Cutoff", 20.0f, 20000.0f, true);
    slot.unit = "Hz";
    auto info = paramInfoFromSlot(slot);
    REQUIRE(info.unit == "Hz");
}

TEST_CASE("paramInfoFromSlot - unit range Mix displays as percent", "[faust][paraminfo]") {
    auto info = paramInfoFromSlot(makeContinuousSlot(0, "Mix", 0.0f, 1.0f));
    REQUIRE(info.displayFormat == DisplayFormat::Percent);
}

TEST_CASE("paramInfoFromSlot - paramIndex preserved", "[faust][paraminfo]") {
    auto info = paramInfoFromSlot(makeContinuousSlot(42, "X"));
    REQUIRE(info.paramIndex == 42);
}

TEST_CASE("paramInfoFromSlot - author group propagates", "[faust][paraminfo]") {
    auto slot = makeContinuousSlot(3, "Cutoff");
    slot.group = "Filter";
    auto info = paramInfoFromSlot(slot);
    REQUIRE(info.group == "Filter");
}

TEST_CASE("paramInfoFromSlot - tooltip propagates", "[faust][paraminfo]") {
    auto slot = makeContinuousSlot(3, "Cutoff");
    slot.tooltip = "Filter corner frequency";
    REQUIRE(paramInfoFromSlot(slot).tooltip == "Filter corner frequency");
}

TEST_CASE("paramInfoFromSlot - tooltip survives the hidden-slot path", "[faust][paraminfo]") {
    // Hidden slots go through placeholderForInactive, which builds a fresh
    // ParameterInfo and so has to copy the UI-only fields separately.
    auto slot = makeContinuousSlot(3, "Tempo");
    slot.hidden = true;
    slot.tooltip = "Project tempo in BPM";
    REQUIRE(paramInfoFromSlot(slot).tooltip == "Project tempo in BPM");
}

TEST_CASE("paramInfoFromSlot - radio style requests segmented choices", "[faust][paraminfo]") {
    auto slot = makeDiscreteSlot(0, "Voice", {{0.0f, "Mono"}, {1.0f, "Poly"}});
    slot.choiceStyle = FaustChoiceStyle::Radio;
    REQUIRE(paramInfoFromSlot(slot).radioChoices);
}

TEST_CASE("paramInfoFromSlot - menu style stays a dropdown", "[faust][paraminfo]") {
    auto slot = makeDiscreteSlot(0, "Mode", {{0.0f, "Off"}, {1.0f, "On"}});
    slot.choiceStyle = FaustChoiceStyle::Menu;
    auto info = paramInfoFromSlot(slot);
    REQUIRE_FALSE(info.radioChoices);
    // Both styles are still the same discrete parameter underneath.
    REQUIRE(info.scale == ParameterScale::Discrete);
    REQUIRE(info.choices.size() == 2);
}

// ============================================================================
// Boolean
// ============================================================================

TEST_CASE("paramInfoFromSlot - boolean shape", "[faust][paraminfo]") {
    auto info = paramInfoFromSlot(makeBooleanSlot(3, "Bypass"));
    REQUIRE(info.scale == ParameterScale::Boolean);
    REQUIRE(info.minValue == 0.0f);
    REQUIRE(info.maxValue == 1.0f);
    REQUIRE_FALSE(info.modulatable);
}

TEST_CASE("paramInfoFromSlot - boolean default rounded", "[faust][paraminfo]") {
    auto slot = makeBooleanSlot(0, "On");
    slot.defaultValue = 0.7f;  // Faust sometimes emits non-binary defaults
    auto info = paramInfoFromSlot(slot);
    REQUIRE(info.defaultValue == 1.0f);
}

TEST_CASE("paramInfoFromSlot - trigger is a momentary boolean", "[faust][paraminfo]") {
    auto info = paramInfoFromSlot(makeTriggerSlot(4, "Trigger"));
    REQUIRE(info.scale == ParameterScale::Boolean);
    REQUIRE(info.momentary);
    REQUIRE_FALSE(info.modulatable);
}

// ============================================================================
// Hidden
// ============================================================================

TEST_CASE("paramInfoFromSlot - hidden slot degrades to a placeholder",
          "[faust][paraminfo][hidden]") {
    // A `[hidden:1]` slot is still active: the host writes its zone every
    // block, which is the whole point of [role:projecttempo]. It just carries
    // no user-facing parameter, so paramInfoFromSlot returns the same inert
    // placeholder it gives an inactive slot.
    //
    // That is exactly why FaustProcessor::populateParameters filters on
    // `active && !hidden` rather than relying on this: a placeholder still
    // has a valid paramIndex, so pushing it into DeviceInfo::parameters made
    // FaustDeviceLayout render it as a visible "(slot N)" cell.
    auto slot = makeContinuousSlot(3, "BPM", 10.0f, 360.0f);
    slot.hidden = true;

    auto info = paramInfoFromSlot(slot);
    CHECK(info.paramIndex == 3);
    CHECK(info.name != "BPM");
    CHECK(info.name.contains("slot"));
    CHECK_FALSE(info.modulatable);
}

// ============================================================================
// Discrete
// ============================================================================

TEST_CASE("paramInfoFromSlot - discrete choices in value-sorted order", "[faust][paraminfo]") {
    // Provide choices unsorted by value — sorted output must come back
    // in {0,1,2} order regardless of input order.
    auto info = paramInfoFromSlot(
        makeDiscreteSlot(0, "Mode", {{2.0f, "High"}, {0.0f, "Off"}, {1.0f, "Low"}}));
    REQUIRE(info.scale == ParameterScale::Discrete);
    REQUIRE(info.choices.size() == 3);
    REQUIRE(info.choices[0] == "Off");
    REQUIRE(info.choices[1] == "Low");
    REQUIRE(info.choices[2] == "High");
    REQUIRE(info.minValue == 0.0f);
    REQUIRE(info.maxValue == 2.0f);
    REQUIRE_FALSE(info.modulatable);
}

TEST_CASE("paramInfoFromSlot - discrete default lookup by underlying value", "[faust][paraminfo]") {
    auto info = paramInfoFromSlot(makeDiscreteSlot(
        0, "Mode", {{0.0f, "Off"}, {1.0f, "Low"}, {2.0f, "High"}}, /*default=*/2.0f));
    // defaultValue=2.0 should map to the index of the choice whose
    // underlying value is 2 — i.e. "High" at index 2 after sort.
    REQUIRE(info.defaultValue == 2.0f);
}

TEST_CASE("paramInfoFromSlot - discrete with empty choices yields fallback", "[faust][paraminfo]") {
    FaustParamSlot s;
    s.index = 0;
    s.active = true;
    s.label = "Mode";
    s.kind = FaustParamSlot::Kind::Discrete;
    s.choices = {};

    auto info = paramInfoFromSlot(s);
    REQUIRE(info.choices.size() == 1);
    REQUIRE(info.choices[0] == "(empty)");
}
