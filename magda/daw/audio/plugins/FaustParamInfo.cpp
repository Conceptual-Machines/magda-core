#include "FaustParamInfo.hpp"

#include <algorithm>
#include <cmath>

#include "FaustParamPool.hpp"

namespace magda::daw::audio {

namespace {

sdk::ParameterDescriptor placeholderForInactive(const FaustParamSlot& slot) {
    sdk::ParameterDescriptor info;
    info.index = slot.index;
    info.name = "(slot " + std::to_string(slot.index + 1) + ")";
    info.minValue = 0.0f;
    info.maxValue = 1.0f;
    info.defaultValue = 0.0f;
    info.scale = ParameterScale::Linear;
    info.modulatable = false;
    return info;
}

// Both host params share a page, named for what they do. Left ungrouped they
// landed on the generic "Params" page, which says nothing next to a tab
// carrying the patch's own name.
constexpr const char* kHostParamGroup = "Voice";

sdk::ParameterDescriptor voiceModeInfo() {
    sdk::ParameterDescriptor info;
    info.stableId = "voiceMode";
    info.index = FaustParamPool::kSize;
    info.name = "Voice Mode";
    info.group = kHostParamGroup;
    info.minValue = 0.0f;
    info.maxValue = 2.0f;
    info.defaultValue = 0.0f;
    info.scale = ParameterScale::Discrete;
    info.choices = sdk::choicesFromLabels({"Poly", "Mono", "Legato"});
    // Segmented buttons rather than a dropdown: the mode is worth reading at a
    // glance without opening anything.
    info.radioChoices = true;
    // Three segments sharing one cell truncates them to "Pol / Mo / Leg", which
    // is worse than useless - "Mo" could be Mono or Modulation. Three cells is
    // what "Legato" needs to survive at the grid's narrower sizes.
    info.widthCells = 3;
    // Voice allocation is a structural choice, not a sound-design one. Letting
    // an LFO flip it every cycle would just retrigger the flush path.
    info.modulatable = false;
    return info;
}

sdk::ParameterDescriptor glideInfo() {
    sdk::ParameterDescriptor info;
    info.stableId = "glide";
    info.index = FaustParamPool::kSize + 1;
    info.name = "Glide";
    info.group = kHostParamGroup;
    info.unit = "ms";
    info.minValue = 0.0f;
    info.maxValue = 2000.0f;
    info.defaultValue = 0.0f;
    // Linear, and starting at 0: 0 has to mean "off" exactly, which a log
    // scale cannot represent.
    info.scale = ParameterScale::Linear;
    return info;
}

sdk::ParameterDescriptor bendRangeInfo() {
    sdk::ParameterDescriptor info;
    info.stableId = "bendRange";
    info.index = FaustParamPool::kSize + 2;
    info.name = "Bend Range";
    info.group = kHostParamGroup;
    info.unit = "st";
    info.minValue = 0.0f;
    info.maxValue = kMaxBendSemitones;
    // 2 semitones each way is what almost every synth ships with, and what a
    // patch author will assume when they reach for the wheel.
    info.defaultValue = 2.0f;
    info.scale = ParameterScale::Linear;
    return info;
}

sdk::ParameterDescriptor continuousInfo(const FaustParamSlot& slot) {
    sdk::ParameterDescriptor info;
    info.index = slot.index;
    info.name = slot.label.toStdString();
    info.unit = slot.unit.toStdString();
    info.minValue = slot.minValue;
    info.maxValue = slot.maxValue;
    info.defaultValue = slot.defaultValue;
    info.scale = slot.logScale ? ParameterScale::Logarithmic : ParameterScale::Linear;
    if (slot.label.equalsIgnoreCase("Mix") && std::abs(slot.minValue) < 1.0e-6f &&
        std::abs(slot.maxValue - 1.0f) < 1.0e-6f)
        info.displayFormat = DisplayFormat::Percent;
    if (std::isfinite(slot.scaleAnchor))
        info.scaleAnchor = slot.scaleAnchor;
    info.gateSlotIndex = slot.gateSlotIndex;
    info.gateNegated = slot.gateNegated;
    return info;
}

sdk::ParameterDescriptor booleanInfo(const FaustParamSlot& slot) {
    sdk::ParameterDescriptor info;
    info.index = slot.index;
    info.name = slot.label.toStdString();
    info.unit = slot.unit.toStdString();
    info.minValue = 0.0f;
    info.maxValue = 1.0f;
    info.defaultValue = slot.defaultValue >= 0.5f ? 1.0f : 0.0f;
    info.scale = ParameterScale::Boolean;
    info.modulatable = false;  // matches ParameterPresets::boolean
    info.gateSlotIndex = slot.gateSlotIndex;
    info.gateNegated = slot.gateNegated;
    return info;
}

sdk::ParameterDescriptor triggerInfo(const FaustParamSlot& slot) {
    auto info = booleanInfo(slot);
    info.momentary = true;
    return info;
}

sdk::ParameterDescriptor discreteInfo(const FaustParamSlot& slot) {
    sdk::ParameterDescriptor info;
    info.index = slot.index;
    info.name = slot.label.toStdString();
    info.unit = slot.unit.toStdString();
    info.scale = ParameterScale::Discrete;
    info.modulatable = false;  // matches ParameterPresets::discrete
    // `[style:radio{…}]` asks for visible buttons, `[style:menu{…}]` for a
    // dropdown. Both are the same parameter; only the widget differs, and the
    // UI is free to ignore the request when the list is too long for a cell.
    info.radioChoices = (slot.choiceStyle == FaustChoiceStyle::Radio);

    // Sort choices by underlying value, then expose just the labels in
    // that order. ParameterInfo::Discrete indexes choices by
    // round(normalized * (count-1)), so the order of labels here is
    // what the user sees in the dropdown.
    auto sorted = slot.choices;  // copy — we sort in place
    std::sort(sorted.begin(), sorted.end(),
              [](const std::pair<float, juce::String>& a, const std::pair<float, juce::String>& b) {
                  return a.first < b.first;
              });
    std::vector<std::string> labels;
    labels.reserve(sorted.size());
    for (const auto& c : sorted)
        labels.push_back(c.second.toStdString());

    // Defensive fallback: a menu/radio style with no choices degrades to one "(empty)" option
    // so the slot is still selectable.
    if (labels.empty())
        labels.emplace_back("(empty)");

    info.choices = sdk::choicesFromLabels(labels);
    for (size_t i = 0; i < sorted.size(); ++i)
        info.choices[i].value = sorted[i].first;
    info.minValue = 0.0f;
    info.maxValue = static_cast<float>(info.choices.size() - 1);

    // Default → nearest sorted index of the slot's defaultValue (matched
    // by underlying real value, not by sorted position). Falls back to
    // the first choice if no exact match.
    int defaultIndex = 0;
    for (size_t i = 0; i < sorted.size(); ++i) {
        if (sorted[i].first == slot.defaultValue) {
            defaultIndex = static_cast<int>(i);
            break;
        }
    }
    info.defaultValue = static_cast<float>(defaultIndex);
    info.gateSlotIndex = slot.gateSlotIndex;
    info.gateNegated = slot.gateNegated;
    return info;
}

}  // namespace

// The three ids below are the retired plugin's own property spellings, which the
// load-time hydration reads a project saved before the port back onto (#2315).
sdk::ParameterDescriptor faustInstrumentHostParamDescriptor(int hostIndex) {
    switch (hostIndex) {
        case 0:
            return voiceModeInfo();
        case 1:
            return glideInfo();
        default:
            return bendRangeInfo();
    }
}

sdk::ParameterDescriptor paramDescriptorFromSlot(const FaustParamSlot& slot) {
    // Hidden slots are part of the live binding (the host writes to
    // their zones — e.g. ProjectTempo) but should not appear in the
    // inspector. Funnel them through the inactive-placeholder path so
    // the slot index stays addressable for automation lookups while
    // the param grid filters them out by empty name.
    sdk::ParameterDescriptor info;
    if (!slot.active || slot.hidden) {
        info = placeholderForInactive(slot);
        info.group = slot.group.toStdString();
        info.tooltip = slot.tooltip.toStdString();
        info.widthCells = slot.widthCells;
        return info;
    }
    switch (slot.kind) {
        case FaustParamSlot::Kind::Continuous:
            info = continuousInfo(slot);
            break;
        case FaustParamSlot::Kind::Boolean:
            info = booleanInfo(slot);
            break;
        case FaustParamSlot::Kind::Trigger:
            info = triggerInfo(slot);
            break;
        case FaustParamSlot::Kind::Discrete:
            info = discreteInfo(slot);
            break;
    }
    info.group = slot.group.toStdString();
    info.tooltip = slot.tooltip.toStdString();
    info.widthCells = slot.widthCells;
    return info;
}

magda::ParameterInfo paramInfoFromSlot(const FaustParamSlot& slot) {
    return magda::toParameterInfo(paramDescriptorFromSlot(slot));
}

magda::MeterInfo meterInfoFromOutput(const FaustOutputSlot& output) {
    magda::MeterInfo info;
    info.meterIndex = output.index;
    info.name = output.label;
    info.unit = output.unit;
    info.group = output.group;
    info.tooltip = output.tooltip;
    info.minValue = output.minValue;
    info.maxValue = output.maxValue;
    info.widthCells = output.widthCells;
    info.vertical = output.vertical;
    switch (output.style) {
        case FaustOutputStyle::Bar:
            info.style = magda::MeterStyle::Bar;
            break;
        case FaustOutputStyle::Numerical:
            info.style = magda::MeterStyle::Numerical;
            break;
        case FaustOutputStyle::Led:
            info.style = magda::MeterStyle::Led;
            break;
    }
    return info;
}

}  // namespace magda::daw::audio
