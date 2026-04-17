#pragma once

#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

namespace magda {

/**
 * @brief Scale type for parameter value conversion
 *
 * Defines how normalized values (0-1) map to real parameter values.
 */
enum class ParameterScale {
    Linear,       // value = min + normalized * (max - min)
    Logarithmic,  // value = min * pow(max/min, normalized) - for freq, time
    Exponential,  // value = pow(normalized, exponent) * (max - min) + min - for curves
    Discrete,     // value = choices[round(normalized * (count-1))]
    Boolean,      // value = normalized >= 0.5
    FaderDB       // Fader-style dB: 0.75 = 0dB (unity), 0.0 = minDb, 1.0 = maxDb
};

/**
 * @brief How a parameter's value should be formatted for display and parsed from input.
 *
 * Decouples presentation from the scale used for value conversion. For example
 * a volume parameter uses scale=FaderDB and displayFormat=Decibels; a pan uses
 * scale=Linear and displayFormat=Pan. For Default, ParameterUtils::formatValue
 * dispatches on `unit` (Hz/kHz, ms, %, dB, bare) — Default is the right choice
 * for most plugin params.
 */
enum class DisplayFormat {
    Default,   // Dispatch on info.unit (Hz/kHz, dB, %, ms, bare)
    Decibels,  // dB with "-inf" at minValue, always signed ("+3.0", "-6.0")
    Pan,       // -1..+1 → "L100".."C".."R100"
    Percent,   // Treat stored value as 0..1 and display "0%".."100%"
    MidiNote,  // 0..127 → "C-1".."G9"
    Beats,     // float beats: "2.25 beats"
    BarsBeats  // float beats: "1.1.000" bars.beats.ticks (480 ticks/beat)
};

/**
 * @brief Metadata for a plugin parameter
 *
 * Contains all information needed to convert between normalized (0-1)
 * and real parameter values (Hz, ms, dB, etc.), as well as display formatting.
 */
struct ParameterInfo {
    int paramIndex = -1;  // Index within device
    juce::String name;    // "Cutoff", "Resonance", etc.
    juce::String unit;    // "Hz", "ms", "%", "dB", ""

    // Value range — ALL stored in REAL parameter units (Hz, dB, %, …).
    // Consumers never see normalized values here; the normalized↔real
    // conversion lives exclusively in ParameterUtils.
    float minValue = 0.0f;      // Real minimum (e.g., 20.0 for Hz)
    float maxValue = 1.0f;      // Real maximum (e.g., 20000.0 for Hz)
    float defaultValue = 0.5f;  // Real default
    float currentValue = 0.5f;  // Current REAL value (for UI display and sync)

    // Scaling
    ParameterScale scale = ParameterScale::Linear;
    float skewFactor = 1.0f;  // For exponential scaling

    /**
     * The real value that maps to normalized 0.5. 0.0 means "unset" —
     * ParameterUtils then uses the geometric mean for log scales and the
     * arithmetic mean for linear scales. Setting this explicitly lets a
     * parameter place a chosen value at the slider's visual centre
     * (e.g. 1000 Hz for an EQ freq with range [20, 20000]).
     */
    float scaleAnchor = 0.0f;

    // How this parameter's real value is formatted and parsed. Default
    // dispatches on `unit`; override for bespoke shapes (Pan, MidiNote, …).
    DisplayFormat displayFormat = DisplayFormat::Default;

    // Discrete values (if scale == Discrete)
    std::vector<juce::String> choices;  // e.g., {"Off", "Low", "High"}

    // Display text lookup table — getText() results at each normalized step.
    // Index i corresponds to normalized value i/(size-1).
    // Used by the UI formatter instead of computing from scale/range.
    std::vector<juce::String> valueTable;

    // Live display text provider — queries the plugin through TrackManager
    // at call time so the lookup is always safe (returns empty if device is
    // gone). Preferred over valueTable when set — exact values, no quantization.
    // Shared so ParameterInfo copies remain cheap.
    struct DisplayTextProvider {
        int deviceId = -1;
        int paramIndex = -1;
        juce::String format(float normalizedValue) const;
    };
    std::shared_ptr<DisplayTextProvider> displayText;

    // Modulation constraints
    bool modulatable = true;         // Can mods affect this parameter?
    bool bipolarModulation = false;  // Default unipolar; set true for params that need bipolar

    // Default constructor
    ParameterInfo() = default;

    // Constructor with basic info
    ParameterInfo(int index, const juce::String& n, const juce::String& u, float min, float max,
                  float def, ParameterScale s = ParameterScale::Linear)
        : paramIndex(index),
          name(n),
          unit(u),
          minValue(min),
          maxValue(max),
          defaultValue(def),
          scale(s) {}

    /**
     * @brief Whether this parameter's range straddles zero.
     *
     * Used by the automation UI to render a centred "neutral" line and
     * symmetric scale labels for bipolar parameters (EQ gain, pitch,
     * pan, etc.) rather than treating 0 as the bottom of the range.
     * Pan is intentionally excluded since it has its own L/C/R renderer.
     */
    bool isBipolar() const {
        return minValue < 0.0f && maxValue > 0.0f;
    }
};

/**
 * @brief Common parameter presets for typical audio parameters
 */
namespace ParameterPresets {

/**
 * @brief Create a frequency parameter (logarithmic scale)
 * @param index Parameter index
 * @param name Display name
 * @param minHz Minimum frequency in Hz (default: 20)
 * @param maxHz Maximum frequency in Hz (default: 20000)
 */
inline ParameterInfo frequency(int index, const juce::String& name, float minHz = 20.0f,
                               float maxHz = 20000.0f) {
    ParameterInfo info;
    info.paramIndex = index;
    info.name = name;
    info.unit = "Hz";
    info.minValue = minHz;
    info.maxValue = maxHz;
    info.defaultValue = std::sqrt(minHz * maxHz);  // Geometric mean
    info.scale = ParameterScale::Logarithmic;
    return info;
}

/**
 * @brief Create a time parameter (logarithmic scale)
 * @param index Parameter index
 * @param name Display name
 * @param minMs Minimum time in milliseconds (default: 0.1)
 * @param maxMs Maximum time in milliseconds (default: 10000)
 */
inline ParameterInfo time(int index, const juce::String& name, float minMs = 0.1f,
                          float maxMs = 10000.0f) {
    ParameterInfo info;
    info.paramIndex = index;
    info.name = name;
    info.unit = "ms";
    info.minValue = minMs;
    info.maxValue = maxMs;
    info.defaultValue = std::sqrt(minMs * maxMs);  // Geometric mean
    info.scale = ParameterScale::Logarithmic;
    return info;
}

/**
 * @brief Create a percentage parameter (linear 0-100%)
 * @param index Parameter index
 * @param name Display name
 */
inline ParameterInfo percent(int index, const juce::String& name) {
    ParameterInfo info;
    info.paramIndex = index;
    info.name = name;
    info.unit = "%";
    info.minValue = 0.0f;
    info.maxValue = 100.0f;
    info.defaultValue = 50.0f;
    info.scale = ParameterScale::Linear;
    info.displayFormat = DisplayFormat::Percent;
    return info;
}

/**
 * @brief Create a decibels parameter (linear scale in dB)
 * @param index Parameter index
 * @param name Display name
 * @param minDb Minimum dB (default: -60)
 * @param maxDb Maximum dB (default: 12)
 */
inline ParameterInfo decibels(int index, const juce::String& name, float minDb = -60.0f,
                              float maxDb = 12.0f) {
    ParameterInfo info;
    info.paramIndex = index;
    info.name = name;
    info.unit = "dB";
    info.minValue = minDb;
    info.maxValue = maxDb;
    info.defaultValue = 0.0f;  // Unity gain
    info.scale = ParameterScale::Linear;
    info.displayFormat = DisplayFormat::Decibels;
    return info;
}

/**
 * @brief Create a semitones parameter (linear scale for pitch)
 * @param index Parameter index
 * @param name Display name
 * @param minSt Minimum semitones (default: -24, 2 octaves down)
 * @param maxSt Maximum semitones (default: +24, 2 octaves up)
 */
inline ParameterInfo semitones(int index, const juce::String& name, float minSt = -24.0f,
                               float maxSt = 24.0f) {
    ParameterInfo info;
    info.paramIndex = index;
    info.name = name;
    info.unit = "st";
    info.minValue = minSt;
    info.maxValue = maxSt;
    info.defaultValue = 0.0f;  // No pitch shift
    info.scale = ParameterScale::Linear;
    return info;
}

/**
 * @brief Create a boolean/switch parameter
 * @param index Parameter index
 * @param name Display name
 */
inline ParameterInfo boolean(int index, const juce::String& name) {
    ParameterInfo info;
    info.paramIndex = index;
    info.name = name;
    info.unit = "";
    info.minValue = 0.0f;
    info.maxValue = 1.0f;
    info.defaultValue = 0.0f;
    info.scale = ParameterScale::Boolean;
    info.modulatable = false;  // Typically can't modulate on/off switches
    return info;
}

/**
 * @brief Create a discrete choice parameter
 * @param index Parameter index
 * @param name Display name
 * @param choices Available choices
 */
inline ParameterInfo discrete(int index, const juce::String& name,
                              const std::vector<juce::String>& choices) {
    ParameterInfo info;
    info.paramIndex = index;
    info.name = name;
    info.unit = "";
    info.minValue = 0.0f;
    info.maxValue = static_cast<float>(choices.size() - 1);
    info.defaultValue = 0.0f;
    info.scale = ParameterScale::Discrete;
    info.choices = choices;
    info.modulatable = false;  // Typically can't modulate discrete choices
    return info;
}

/**
 * @brief Create a fader-style volume parameter
 *
 * Uses the standard DAW fader scale where:
 * - normalized 0.0 = -60 dB (silence)
 * - normalized 0.75 = 0 dB (unity gain)
 * - normalized 1.0 = +6 dB (max boost)
 *
 * @param index Parameter index
 * @param name Display name
 */
inline ParameterInfo faderVolume(int index, const juce::String& name) {
    ParameterInfo info;
    info.paramIndex = index;
    info.name = name;
    info.unit = "dB";
    info.minValue = -60.0f;
    info.maxValue = 6.0f;
    info.defaultValue = 0.0f;  // Unity gain
    info.scale = ParameterScale::FaderDB;
    info.displayFormat = DisplayFormat::Decibels;
    return info;
}

/**
 * @brief Create a pan parameter (-100% L to +100% R)
 * @param index Parameter index
 * @param name Display name
 */
inline ParameterInfo pan(int index, const juce::String& name) {
    ParameterInfo info;
    info.paramIndex = index;
    info.name = name;
    info.unit = "";  // Uses L/C/R display
    info.minValue = -1.0f;
    info.maxValue = 1.0f;
    info.defaultValue = 0.0f;  // Center
    info.scale = ParameterScale::Linear;
    info.displayFormat = DisplayFormat::Pan;
    return info;
}

}  // namespace ParameterPresets

}  // namespace magda
