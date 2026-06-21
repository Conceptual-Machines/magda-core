#pragma once

#include <juce_core/juce_core.h>

namespace magda {

enum class TechnicalTextToken {
    Percent,
    Dpi,
    Hertz,
    Kilohertz,
    Decibels,
    Midi,
    Milliseconds,
    Seconds,
    ShortSeconds,
    Bars,
    Beats,
    Bpm,
    Semitones,
    SemitonesName,
    Cents,
    Discrete,
    Boolean,
    Master,
    PanCenter,
    PanLeft,
    PanRight,
};

inline juce::String technicalText(TechnicalTextToken token) {
    switch (token) {
        case TechnicalTextToken::Percent:
            return "%";
        case TechnicalTextToken::Dpi:
            return "DPI";
        case TechnicalTextToken::Hertz:
            return "Hz";
        case TechnicalTextToken::Kilohertz:
            return "kHz";
        case TechnicalTextToken::Decibels:
            return "dB";
        case TechnicalTextToken::Midi:
            return "MIDI";
        case TechnicalTextToken::Milliseconds:
            return "ms";
        case TechnicalTextToken::Seconds:
            return "s";
        case TechnicalTextToken::ShortSeconds:
            return "sec";
        case TechnicalTextToken::Bars:
            return "bars";
        case TechnicalTextToken::Beats:
            return "beats";
        case TechnicalTextToken::Bpm:
            return "BPM";
        case TechnicalTextToken::Semitones:
            return "st";
        case TechnicalTextToken::SemitonesName:
            return "semitones";
        case TechnicalTextToken::Cents:
            return "cents";
        case TechnicalTextToken::Discrete:
            return "discrete";
        case TechnicalTextToken::Boolean:
            return "boolean";
        case TechnicalTextToken::Master:
            return "Master";
        case TechnicalTextToken::PanCenter:
            return "C";
        case TechnicalTextToken::PanLeft:
            return "L";
        case TechnicalTextToken::PanRight:
            return "R";
    }

    return {};
}

inline juce::String technicalTextSuffix(TechnicalTextToken token, bool leadingSpace = true) {
    const auto text = technicalText(token);
    return leadingSpace && text.isNotEmpty() ? " " + text : text;
}

}  // namespace magda
