#include "FaustParamInfo.hpp"

#include <algorithm>
#include <cmath>

namespace magda::daw::audio {

namespace {

magda::ParameterInfo placeholderForInactive(const FaustParamSlot& slot) {
    magda::ParameterInfo info;
    info.paramIndex = slot.index;
    info.name = juce::String("(slot ") + juce::String(slot.index + 1) + ")";
    info.minValue = 0.0f;
    info.maxValue = 1.0f;
    info.defaultValue = 0.0f;
    info.scale = magda::ParameterScale::Linear;
    info.modulatable = false;
    return info;
}

magda::ParameterInfo continuousInfo(const FaustParamSlot& slot) {
    magda::ParameterInfo info;
    info.paramIndex = slot.index;
    info.name = slot.label;
    info.unit = slot.unit;
    info.minValue = slot.minValue;
    info.maxValue = slot.maxValue;
    info.defaultValue = slot.defaultValue;
    info.currentValue = slot.defaultValue;
    info.scale = slot.logScale ? magda::ParameterScale::Logarithmic : magda::ParameterScale::Linear;
    if (slot.label.equalsIgnoreCase("Mix") && std::abs(slot.minValue) < 1.0e-6f &&
        std::abs(slot.maxValue - 1.0f) < 1.0e-6f)
        info.displayFormat = magda::DisplayFormat::Percent;
    if (std::isfinite(slot.scaleAnchor))
        info.scaleAnchor = slot.scaleAnchor;
    info.gateSlotIndex = slot.gateSlotIndex;
    info.gateNegated = slot.gateNegated;
    return info;
}

magda::ParameterInfo booleanInfo(const FaustParamSlot& slot) {
    magda::ParameterInfo info;
    info.paramIndex = slot.index;
    info.name = slot.label;
    info.unit = slot.unit;
    info.minValue = 0.0f;
    info.maxValue = 1.0f;
    info.defaultValue = slot.defaultValue >= 0.5f ? 1.0f : 0.0f;
    info.currentValue = info.defaultValue;
    info.scale = magda::ParameterScale::Boolean;
    info.modulatable = false;  // matches ParameterPresets::boolean
    info.gateSlotIndex = slot.gateSlotIndex;
    info.gateNegated = slot.gateNegated;
    return info;
}

magda::ParameterInfo discreteInfo(const FaustParamSlot& slot) {
    magda::ParameterInfo info;
    info.paramIndex = slot.index;
    info.name = slot.label;
    info.unit = slot.unit;
    info.scale = magda::ParameterScale::Discrete;
    info.modulatable = false;  // matches ParameterPresets::discrete

    // Sort choices by underlying value, then expose just the labels in
    // that order. ParameterInfo::Discrete indexes choices by
    // round(normalized * (count-1)), so the order of labels here is
    // what the user sees in the dropdown.
    auto sorted = slot.choices;  // copy — we sort in place
    std::sort(sorted.begin(), sorted.end(),
              [](const std::pair<float, juce::String>& a, const std::pair<float, juce::String>& b) {
                  return a.first < b.first;
              });
    info.choices.reserve(sorted.size());
    for (const auto& c : sorted)
        info.choices.push_back(c.second);

    if (info.choices.empty()) {
        // Defensive fallback — Phase 2 metadata parser shouldn't emit
        // an empty choice list for a menu/radio style, but if it does
        // we degrade to a single "(empty)" option so the slot is still
        // selectable.
        info.choices.push_back("(empty)");
    }
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
    info.currentValue = info.defaultValue;
    info.gateSlotIndex = slot.gateSlotIndex;
    info.gateNegated = slot.gateNegated;
    return info;
}

}  // namespace

magda::ParameterInfo paramInfoFromSlot(const FaustParamSlot& slot) {
    // Hidden slots are part of the live binding (the host writes to
    // their zones — e.g. ProjectTempo) but should not appear in the
    // inspector. Funnel them through the inactive-placeholder path so
    // the slot index stays addressable for automation lookups while
    // the param grid filters them out by empty name.
    if (!slot.active || slot.hidden)
        return placeholderForInactive(slot);

    switch (slot.kind) {
        case FaustParamSlot::Kind::Continuous:
            return continuousInfo(slot);
        case FaustParamSlot::Kind::Boolean:
            return booleanInfo(slot);
        case FaustParamSlot::Kind::Discrete:
            return discreteInfo(slot);
    }
    return placeholderForInactive(slot);
}

}  // namespace magda::daw::audio
