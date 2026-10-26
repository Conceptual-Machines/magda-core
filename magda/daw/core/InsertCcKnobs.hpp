#pragma once

#include "DeviceInfo.hpp"

/**
 * @file InsertCcKnobs.hpp
 * @brief An External Insert's MIDI CC knobs: each a device parameter, 0 to 127, sent as one CC
 *        number to the hardware (#3164).
 */

namespace magda::insert_cc {

/// Most knobs one insert holds: four pages of eight.
constexpr int kMaxKnobs = 32;
constexpr int kKnobsPerPage = 8;

/// The CC a new knob sends when nothing else is free.
constexpr int kDefaultCcNumber = 1;

/// The knob holding parameter @p paramIndex, or null.
const InsertConfig::CcSlot* findKnob(const DeviceInfo& device, int paramIndex);

/// Adds a knob sending the lowest CC the insert does not send yet. Its parameter index, or -1 when
/// the insert already has kMaxKnobs.
int addKnob(DeviceInfo& device);

/// Removes the knob holding @p paramIndex, and its parameter.
void removeKnob(DeviceInfo& device, int paramIndex);

/// Points the knob holding @p paramIndex at CC @p ccNumber, renaming its parameter to match.
void setKnobCc(DeviceInfo& device, int paramIndex, int ccNumber);

/// Makes the insert's knobs send @p ccNumbers, in order. Knobs already there keep their parameters,
/// and so their lanes and links; the rest are added or removed. False past kMaxKnobs.
bool setKnobs(DeviceInfo& device, const std::vector<int>& ccNumbers);

/// What a knob's parameter is called: its CC number, which is what its automation lane shows.
juce::String knobName(int ccNumber);

}  // namespace magda::insert_cc
