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
    // Modifier-key keycap symbols — universal, never translated
    Shift,
    // Proper nouns / formats — never translated, kept at base (Latin) font size
    Magda,
    Audio,
    DawProject,
    Vst3,
    Au,
    Json,
    Flac,
    Rgb,
    Hex,
    Lua,
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
        case TechnicalTextToken::Shift:
            // U+21E7 UPWARDS WHITE ARROW — the keycap shift symbol
            return juce::String::fromUTF8("\xe2\x87\xa7");
        case TechnicalTextToken::Magda:
            return "MAGDA";
        case TechnicalTextToken::Audio:
            return "Audio";
        case TechnicalTextToken::DawProject:
            return "DAWproject";
        case TechnicalTextToken::Vst3:
            return "VST3";
        case TechnicalTextToken::Au:
            return "AU";
        case TechnicalTextToken::Json:
            return "JSON";
        case TechnicalTextToken::Flac:
            return "FLAC";
        case TechnicalTextToken::Rgb:
            return "RGB";
        case TechnicalTextToken::Hex:
            return "Hex";
        case TechnicalTextToken::Lua:
            return "Lua";
    }

    return {};
}

inline juce::String technicalTextSuffix(TechnicalTextToken token, bool leadingSpace = true) {
    const auto text = technicalText(token);
    return leadingSpace && text.isNotEmpty() ? " " + text : text;
}

}  // namespace magda
