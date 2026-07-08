#pragma once

#include <juce_core/juce_core.h>

#include <cstddef>

#include "core/ModInfo.hpp"

namespace magda::daw::ui {

// Shared stepped-slider mapping for tempo-sync divisions, used by the
// modulator editor panel and the Sidechain faceplate. Order is slow to fast:
// multi-bar lengths first, then 1 Bar, then for each note (Half, Quarter,
// Eighth, Sixteenth, ThirtySecond): dotted -> normal -> triplet. Labels match
// index-for-index.
inline constexpr magda::SyncDivision kSyncDivisionOrder[] = {
    magda::SyncDivision::SixteenBars,         // 16 Bars
    magda::SyncDivision::EightBars,           // 8 Bars
    magda::SyncDivision::FourBars,            // 4 Bars
    magda::SyncDivision::TwoBars,             // 2 Bars
    magda::SyncDivision::Whole,               // 1 Bar
    magda::SyncDivision::DottedHalf,          // 1/2.
    magda::SyncDivision::Half,                // 1/2
    magda::SyncDivision::TripletHalf,         // 1/2T
    magda::SyncDivision::DottedQuarter,       // 1/4.
    magda::SyncDivision::Quarter,             // 1/4
    magda::SyncDivision::TripletQuarter,      // 1/4T
    magda::SyncDivision::DottedEighth,        // 1/8.
    magda::SyncDivision::Eighth,              // 1/8
    magda::SyncDivision::TripletEighth,       // 1/8T
    magda::SyncDivision::DottedSixteenth,     // 1/16.
    magda::SyncDivision::Sixteenth,           // 1/16
    magda::SyncDivision::TripletSixteenth,    // 1/16T
    magda::SyncDivision::DottedThirtySecond,  // 1/32.
    magda::SyncDivision::ThirtySecond,        // 1/32
    magda::SyncDivision::TripletThirtySecond  // 1/32T
};

inline constexpr const char* kSyncDivisionLabels[] = {
    "16 Bars", "8 Bars", "4 Bars", "2 Bars", "1 Bar", "1/2.", "1/2",   "1/2T",  "1/4.", "1/4",
    "1/4T",    "1/8.",   "1/8",    "1/8T",   "1/16.", "1/16", "1/16T", "1/32.", "1/32", "1/32T"};

inline constexpr int kNumSyncDivisions = static_cast<int>(std::size(kSyncDivisionOrder));

static_assert(std::size(kSyncDivisionOrder) == std::size(kSyncDivisionLabels),
              "division order and labels must stay index-aligned");

inline int syncDivisionToIndex(magda::SyncDivision d) {
    for (int i = 0; i < kNumSyncDivisions; ++i)
        if (kSyncDivisionOrder[i] == d)
            return i;
    // Quarter — find its position dynamically so this stays correct if the
    // order array is reshuffled.
    for (int i = 0; i < kNumSyncDivisions; ++i)
        if (kSyncDivisionOrder[i] == magda::SyncDivision::Quarter)
            return i;
    return 0;
}

inline magda::SyncDivision indexToSyncDivision(int idx) {
    if (idx < 0 || idx >= kNumSyncDivisions)
        return magda::SyncDivision::Quarter;
    return kSyncDivisionOrder[idx];
}

inline juce::String syncDivisionLabelForIndex(int idx) {
    if (idx < 0 || idx >= kNumSyncDivisions)
        return {};
    return kSyncDivisionLabels[idx];
}

}  // namespace magda::daw::ui
