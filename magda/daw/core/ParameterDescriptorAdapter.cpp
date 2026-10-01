#include "ParameterDescriptorAdapter.hpp"

namespace magda {

juce::String toJuceString(const std::string& text) {
    return juce::String(juce::CharPointer_UTF8(text.c_str()));
}

std::vector<juce::String> choiceLabels(const std::vector<sdk::ParameterChoice>& choices) {
    std::vector<juce::String> labels;
    labels.reserve(choices.size());
    for (const auto& choice : choices)
        labels.push_back(toJuceString(choice.label));
    return labels;
}

ParameterInfo toParameterInfo(const sdk::ParameterDescriptor& descriptor) {
    ParameterInfo info;
    info.paramIndex = descriptor.index;
    info.stableId = toJuceString(descriptor.stableId);
    info.name = toJuceString(descriptor.name);
    info.unit = toJuceString(descriptor.unit);
    info.group = toJuceString(descriptor.group);
    info.tooltip = toJuceString(descriptor.tooltip);
    info.widthCells = descriptor.widthCells;
    info.minValue = descriptor.minValue;
    info.maxValue = descriptor.maxValue;
    info.valueConvention = descriptor.valueConvention;
    info.defaultValue = descriptor.defaultValue;
    info.currentValue = descriptor.defaultValue;
    info.scale = descriptor.scale;
    info.skewFactor = descriptor.exponent;
    info.unityPosition = descriptor.unityPosition;
    info.unityDb = descriptor.unityDb;
    info.scaleAnchor = descriptor.scaleAnchor;
    info.displayFormat = descriptor.displayFormat;
    info.choices = choiceLabels(descriptor.choices);
    info.radioChoices = descriptor.radioChoices;
    for (const auto& tick : descriptor.labelTicks)
        info.labelTicks.emplace_back(tick.value, toJuceString(tick.label));
    info.gateSlotIndex = descriptor.gateSlotIndex;
    info.gateNegated = descriptor.gateNegated;
    info.hidden = descriptor.hidden;
    info.momentary = descriptor.momentary;
    info.wrapperRole = descriptor.wrapperRole;
    info.modulatable = descriptor.modulatable;
    info.bipolarModulation = descriptor.bipolarModulation;
    return info;
}

}  // namespace magda
