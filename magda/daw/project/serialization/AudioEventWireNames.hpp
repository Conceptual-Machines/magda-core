// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file AudioEventWireNames.hpp
 * @brief The strings an audio event's enums are saved as. Frozen: files hold them.
 */

#pragma once

#include <juce_core/juce_core.h>

#include <optional>

#include "../../core/ClipInfo.hpp"

namespace magda::audio_event_wire {

inline const char* toString(Provenance from) {
    switch (from) {
        case Provenance::FileMetadata:
            return "fileMetadata";
        case Provenance::Analysis:
            return "analysis";
        case Provenance::User:
            return "user";
        case Provenance::Project:
            return "project";
        case Provenance::None:
            break;
    }
    return "none";
}

inline const char* toString(RegionExtent extent) {
    switch (extent) {
        case RegionExtent::Interpretation:
            return "interpretation";
        case RegionExtent::Explicit:
            return "explicit";
        case RegionExtent::WholeSource:
            break;
    }
    return "wholeSource";
}

inline const char* toString(LoopLengthIntent intent) {
    return intent == LoopLengthIntent::Musical ? "musical" : "source";
}

inline const char* toString(PlaybackIntent intent) {
    return intent == PlaybackIntent::Beat ? "beat" : "free";
}

inline std::optional<Provenance> provenanceFrom(const juce::String& text) {
    for (auto value : {Provenance::None, Provenance::FileMetadata, Provenance::Analysis,
                       Provenance::User, Provenance::Project})
        if (text == toString(value))
            return value;
    return std::nullopt;
}

inline std::optional<RegionExtent> regionExtentFrom(const juce::String& text) {
    for (auto value :
         {RegionExtent::WholeSource, RegionExtent::Interpretation, RegionExtent::Explicit})
        if (text == toString(value))
            return value;
    return std::nullopt;
}

inline std::optional<LoopLengthIntent> loopLengthIntentFrom(const juce::String& text) {
    for (auto value : {LoopLengthIntent::Source, LoopLengthIntent::Musical})
        if (text == toString(value))
            return value;
    return std::nullopt;
}

inline std::optional<PlaybackIntent> playbackIntentFrom(const juce::String& text) {
    for (auto value : {PlaybackIntent::Free, PlaybackIntent::Beat})
        if (text == toString(value))
            return value;
    return std::nullopt;
}

}  // namespace magda::audio_event_wire
