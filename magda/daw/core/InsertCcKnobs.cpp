#include "InsertCcKnobs.hpp"

#include <algorithm>

namespace magda::insert_cc {

namespace {

ParameterInfo knobParameter(int paramIndex, int ccNumber) {
    ParameterInfo info;
    info.paramIndex = paramIndex;
    info.stableId = "cc" + juce::String(paramIndex);
    info.name = knobName(ccNumber);
    info.minValue = 0.0f;
    info.maxValue = 127.0f;
    info.defaultValue = 0.0f;
    info.currentValue = 0.0f;
    // Whole steps: the hardware hears 128 values, so the knob shows those.
    for (int value = 0; value <= 127; ++value)
        info.valueTable.emplace_back(value);
    return info;
}

}  // namespace

bool setKnobs(DeviceInfo& device, const std::vector<int>& ccNumbers) {
    if (static_cast<int>(ccNumbers.size()) > kMaxKnobs)
        return false;
    while (device.insert.ccSlots.size() > ccNumbers.size())
        removeKnob(device, device.insert.ccSlots.back().paramIndex);
    while (device.insert.ccSlots.size() < ccNumbers.size())
        addKnob(device);
    for (std::size_t i = 0; i < ccNumbers.size(); ++i)
        setKnobCc(device, device.insert.ccSlots[i].paramIndex, ccNumbers[i]);
    return true;
}

juce::String knobName(int ccNumber) {
    return "CC " + juce::String(ccNumber);
}

const InsertConfig::CcSlot* findKnob(const DeviceInfo& device, int paramIndex) {
    const auto& slots = device.insert.ccSlots;
    const auto found = std::find_if(slots.begin(), slots.end(), [paramIndex](const auto& slot) {
        return slot.paramIndex == paramIndex;
    });
    return found != slots.end() ? &*found : nullptr;
}

int addKnob(DeviceInfo& device) {
    auto& insert = device.insert;
    if (static_cast<int>(insert.ccSlots.size()) >= kMaxKnobs)
        return -1;

    int ccNumber = kDefaultCcNumber;
    for (int candidate = 1; candidate < 128; ++candidate) {
        const bool taken =
            std::any_of(insert.ccSlots.begin(), insert.ccSlots.end(),
                        [candidate](const auto& slot) { return slot.ccNumber == candidate; });
        if (!taken) {
            ccNumber = candidate;
            break;
        }
    }

    // Past every index the device has ever used, so a removed knob's lanes stay unreachable.
    int paramIndex = insert.nextCcParamIndex;
    for (const auto& param : device.parameters)
        paramIndex = std::max(paramIndex, param.paramIndex + 1);
    insert.nextCcParamIndex = paramIndex + 1;

    insert.ccSlots.push_back({.paramIndex = paramIndex, .ccNumber = ccNumber});
    device.parameters.push_back(knobParameter(paramIndex, ccNumber));
    return paramIndex;
}

void removeKnob(DeviceInfo& device, int paramIndex) {
    std::erase_if(device.insert.ccSlots,
                  [paramIndex](const auto& slot) { return slot.paramIndex == paramIndex; });
    std::erase_if(device.parameters,
                  [paramIndex](const auto& param) { return param.paramIndex == paramIndex; });
}

void setKnobCc(DeviceInfo& device, int paramIndex, int ccNumber) {
    ccNumber = juce::jlimit(0, 127, ccNumber);
    for (auto& slot : device.insert.ccSlots)
        if (slot.paramIndex == paramIndex)
            slot.ccNumber = ccNumber;
    for (auto& param : device.parameters)
        if (param.paramIndex == paramIndex)
            param.name = knobName(ccNumber);
}

}  // namespace magda::insert_cc
