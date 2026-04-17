#include "ParameterUtils.hpp"

#include <cmath>
#include <cstdio>
#include <limits>

namespace magda {
namespace ParameterUtils {

namespace {

// Compute the skew exponent that places `anchorPosition` (the normalized
// position of the anchor in the scale's natural space — linear ratio for
// Linear, log ratio for Logarithmic) at the visual midpoint 0.5.
//
// Returns 1.0 (no skew) when the anchor already sits at 0.5 or when the
// anchor is degenerate (0 or 1). Clamps to a sane range so extreme anchors
// don't produce pathological curves.
float computeSkew(float anchorPosition) {
    if (anchorPosition <= 1e-6f || anchorPosition >= 1.0f - 1e-6f)
        return 1.0f;
    if (std::abs(anchorPosition - 0.5f) < 1e-6f)
        return 1.0f;
    // pow(0.5, skew) should equal anchorPosition, so skew = log(anchor)/log(0.5).
    return std::log(anchorPosition) / std::log(0.5f);
}

// True when info.scaleAnchor is set and strictly inside (min, max).
bool hasValidAnchor(const ParameterInfo& info) {
    return info.scaleAnchor > info.minValue && info.scaleAnchor < info.maxValue;
}

}  // namespace

float normalizedToReal(float normalized, const ParameterInfo& info) {
    // Clamp input to valid range
    normalized = juce::jlimit(0.0f, 1.0f, normalized);

    switch (info.scale) {
        case ParameterScale::Linear: {
            float range = info.maxValue - info.minValue;
            if (hasValidAnchor(info) && range > 0.0f) {
                float anchorPos = (info.scaleAnchor - info.minValue) / range;
                float skew = computeSkew(anchorPos);
                normalized = std::pow(normalized, skew);
            }
            return info.minValue + normalized * range;
        }

        case ParameterScale::Logarithmic: {
            // Fallback to linear when log is undefined (non-positive min).
            if (info.minValue <= 0.0f) {
                return info.minValue + normalized * (info.maxValue - info.minValue);
            }
            float logRange = std::log(info.maxValue / info.minValue);
            if (hasValidAnchor(info)) {
                // Place anchor at norm=0.5 in log space by skewing norm first.
                float anchorLogPos = std::log(info.scaleAnchor / info.minValue) / logRange;
                float skew = computeSkew(anchorLogPos);
                normalized = std::pow(normalized, skew);
            }
            return info.minValue * std::exp(normalized * logRange);
        }

        case ParameterScale::Exponential:
            return std::pow(normalized, info.skewFactor) * (info.maxValue - info.minValue) +
                   info.minValue;

        case ParameterScale::Discrete: {
            if (info.choices.empty()) {
                return 0.0f;
            }
            int index = static_cast<int>(std::round(normalized * (info.choices.size() - 1)));
            return static_cast<float>(index);
        }

        case ParameterScale::Boolean:
            return normalized >= 0.5f ? 1.0f : 0.0f;

        case ParameterScale::FaderDB: {
            // Fader-style dB scale: 0.75 = 0dB (unity)
            // 0.0 = minValue (e.g., -60dB), 1.0 = maxValue (e.g., +6dB)
            constexpr float UNITY_POS = 0.75f;
            constexpr float UNITY_DB = 0.0f;

            if (normalized <= 0.0f)
                return info.minValue;
            if (normalized >= 1.0f)
                return info.maxValue;

            if (normalized < UNITY_POS) {
                // Below unity: 0..0.75 maps to minValue..0dB
                return info.minValue + (normalized / UNITY_POS) * (UNITY_DB - info.minValue);
            } else {
                // Above unity: 0.75..1.0 maps to 0dB..maxValue
                return UNITY_DB +
                       ((normalized - UNITY_POS) / (1.0f - UNITY_POS)) * (info.maxValue - UNITY_DB);
            }
        }

        default:
            return info.minValue + normalized * (info.maxValue - info.minValue);
    }
}

float realToNormalized(float real, const ParameterInfo& info) {
    switch (info.scale) {
        case ParameterScale::Linear: {
            float range = info.maxValue - info.minValue;
            if (range == 0.0f)
                return 0.0f;
            float linPos = (real - info.minValue) / range;
            if (hasValidAnchor(info)) {
                float anchorPos = (info.scaleAnchor - info.minValue) / range;
                float skew = computeSkew(anchorPos);
                // Invert the forward skew (norm -> norm^skew) with norm -> norm^(1/skew).
                linPos = std::pow(juce::jlimit(0.0f, 1.0f, linPos), 1.0f / skew);
            }
            return juce::jlimit(0.0f, 1.0f, linPos);
        }

        case ParameterScale::Logarithmic: {
            // Handle edge cases
            if (info.minValue <= 0.0f || real <= 0.0f) {
                float range = info.maxValue - info.minValue;
                if (range == 0.0f)
                    return 0.0f;
                return juce::jlimit(0.0f, 1.0f, (real - info.minValue) / range);
            }
            float logRange = std::log(info.maxValue / info.minValue);
            if (logRange == 0.0f)
                return 0.0f;
            float logPos = std::log(real / info.minValue) / logRange;
            if (hasValidAnchor(info)) {
                float anchorLogPos = std::log(info.scaleAnchor / info.minValue) / logRange;
                float skew = computeSkew(anchorLogPos);
                logPos = std::pow(juce::jlimit(0.0f, 1.0f, logPos), 1.0f / skew);
            }
            return juce::jlimit(0.0f, 1.0f, logPos);
        }

        case ParameterScale::Exponential: {
            float range = info.maxValue - info.minValue;
            if (range == 0.0f || info.skewFactor == 0.0f)
                return 0.0f;
            float normalized = (real - info.minValue) / range;
            return juce::jlimit(0.0f, 1.0f, std::pow(normalized, 1.0f / info.skewFactor));
        }

        case ParameterScale::Discrete: {
            if (info.choices.empty())
                return 0.0f;
            int index = juce::jlimit(0, static_cast<int>(info.choices.size() - 1),
                                     static_cast<int>(std::round(real)));
            return static_cast<float>(index) / static_cast<float>(info.choices.size() - 1);
        }

        case ParameterScale::Boolean:
            return real >= 0.5f ? 1.0f : 0.0f;

        case ParameterScale::FaderDB: {
            // Fader-style dB scale: 0.75 = 0dB (unity)
            constexpr float UNITY_POS = 0.75f;
            constexpr float UNITY_DB = 0.0f;

            if (real <= info.minValue)
                return 0.0f;
            if (real >= info.maxValue)
                return 1.0f;

            if (real < UNITY_DB) {
                // Below unity: minValue..0dB maps to 0..0.75
                return UNITY_POS * (real - info.minValue) / (UNITY_DB - info.minValue);
            } else {
                // Above unity: 0dB..maxValue maps to 0.75..1.0
                return UNITY_POS +
                       (1.0f - UNITY_POS) * (real - UNITY_DB) / (info.maxValue - UNITY_DB);
            }
        }

        default: {
            float range = info.maxValue - info.minValue;
            if (range == 0.0f)
                return 0.0f;
            return juce::jlimit(0.0f, 1.0f, (real - info.minValue) / range);
        }
    }
}

float applyModulation(float baseNormalized, float modValue, float amount, bool bipolar) {
    // modValue is 0-1, convert to -1 to +1 if bipolar
    float modOffset = bipolar ? (modValue * 2.0f - 1.0f) : modValue;

    // amount is depth: how much of the mod affects the base
    float delta = modOffset * amount;

    return juce::jlimit(0.0f, 1.0f, baseNormalized + delta);
}

float applyModulations(float baseNormalized,
                       const std::vector<std::pair<float, float>>& modsAndAmounts, bool bipolar) {
    float result = baseNormalized;

    for (const auto& [modValue, amount] : modsAndAmounts) {
        float modOffset = bipolar ? (modValue * 2.0f - 1.0f) : modValue;
        result += modOffset * amount;
    }

    return juce::jlimit(0.0f, 1.0f, result);
}

namespace {

juce::String formatDecibels(float db, int decimalPlaces) {
    if (!std::isfinite(db) || db <= -60.0f + 0.001f)
        return "-inf";
    juce::String sign = db > 0.0f ? "+" : "";
    return sign + juce::String(db, decimalPlaces) + " dB";
}

juce::String formatHz(float hz, int decimalPlaces) {
    if (hz >= 1000.0f)
        return juce::String(hz / 1000.0f, decimalPlaces) + " kHz";
    return juce::String(hz, decimalPlaces) + " Hz";
}

juce::String formatMs(float ms, int decimalPlaces) {
    if (ms >= 1000.0f)
        return juce::String(ms / 1000.0f, decimalPlaces) + " s";
    return juce::String(ms, decimalPlaces) + " ms";
}

juce::String formatPan(float value) {
    if (std::abs(value) < 0.005f)
        return "C";
    if (value < 0.0f)
        return "L" + juce::String(static_cast<int>(std::round(-value * 100.0f)));
    return "R" + juce::String(static_cast<int>(std::round(value * 100.0f)));
}

juce::String formatMidiNote(float value) {
    int n = juce::jlimit(0, 127, static_cast<int>(std::round(value)));
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int octave = (n / 12) - 1;
    return juce::String(names[n % 12]) + juce::String(octave);
}

juce::String formatBars(float beats, int beatsPerBar = 4) {
    constexpr int TICKS_PER_BEAT = 480;
    int bars = static_cast<int>(std::floor(beats / beatsPerBar));
    float rem = beats - bars * beatsPerBar;
    int b = static_cast<int>(std::floor(rem));
    int ticks = static_cast<int>(std::round((rem - b) * TICKS_PER_BEAT));
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d.%d.%03d", bars + 1, b + 1, ticks);
    return juce::String(buf);
}

std::optional<float> parseDecibels(juce::String text) {
    auto lower = text.trim().toLowerCase();
    if (lower == "-inf" || lower == "-infinity" || lower == "inf")
        return lower.startsWith("-") ? -std::numeric_limits<float>::infinity()
                                     : std::numeric_limits<float>::infinity();
    if (lower.endsWith("db"))
        lower = lower.dropLastCharacters(2).trim();
    if (lower.isEmpty())
        return std::nullopt;
    bool ok = lower.containsAnyOf("0123456789");
    if (!ok)
        return std::nullopt;
    return static_cast<float>(lower.getDoubleValue());
}

std::optional<float> parsePan(juce::String text) {
    auto t = text.trim().toLowerCase();
    if (t == "c" || t == "center" || t == "centre")
        return 0.0f;
    if (t.startsWith("l")) {
        auto num = t.substring(1).trim();
        if (num.isEmpty())
            return std::nullopt;
        return static_cast<float>(-num.getDoubleValue() / 100.0);
    }
    if (t.startsWith("r")) {
        auto num = t.substring(1).trim();
        if (num.isEmpty())
            return std::nullopt;
        return static_cast<float>(num.getDoubleValue() / 100.0);
    }
    if (t.isEmpty())
        return std::nullopt;
    // Bare number: treat as -100..+100
    return static_cast<float>(t.getDoubleValue() / 100.0);
}

std::optional<float> parseMidiNote(juce::String text) {
    auto t = text.trim();
    if (t.isEmpty())
        return std::nullopt;
    // Try numeric first
    if (t.containsOnly("0123456789-. "))
        return static_cast<float>(t.getDoubleValue());
    // Parse note name (sharps and flats, case-insensitive)
    auto lower = t.toLowerCase();
    static const char* sharp[] = {"c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b"};
    static const char* flat[] = {"c", "db", "d", "eb", "e", "f", "gb", "g", "ab", "a", "bb", "b"};
    int note = -1, consumed = 0;
    for (int i = 0; i < 12; ++i) {
        juce::String s(sharp[i]), f(flat[i]);
        if (lower.startsWith(s) && s.length() > consumed) {
            note = i;
            consumed = s.length();
        }
        if (lower.startsWith(f) && f.length() > consumed) {
            note = i;
            consumed = f.length();
        }
    }
    if (note < 0)
        return std::nullopt;
    auto rest = t.substring(consumed).trim();
    int octave = rest.getIntValue();
    return static_cast<float>((octave + 1) * 12 + note);
}

std::optional<float> parseHz(juce::String text) {
    auto lower = text.trim().toLowerCase();
    if (lower.endsWith("khz")) {
        auto num = lower.dropLastCharacters(3).trim();
        if (num.isEmpty())
            return std::nullopt;
        return static_cast<float>(num.getDoubleValue() * 1000.0);
    }
    if (lower.endsWith("hz"))
        lower = lower.dropLastCharacters(2).trim();
    if (lower.endsWith("k"))
        return static_cast<float>(lower.dropLastCharacters(1).trim().getDoubleValue() * 1000.0);
    if (lower.isEmpty())
        return std::nullopt;
    return static_cast<float>(lower.getDoubleValue());
}

std::optional<float> parseMs(juce::String text) {
    auto lower = text.trim().toLowerCase();
    if (lower.endsWith("ms"))
        lower = lower.dropLastCharacters(2).trim();
    else if (lower.endsWith("s"))
        return static_cast<float>(lower.dropLastCharacters(1).trim().getDoubleValue() * 1000.0);
    if (lower.isEmpty())
        return std::nullopt;
    return static_cast<float>(lower.getDoubleValue());
}

}  // namespace

juce::String formatValue(float realValue, const ParameterInfo& info, int decimalPlaces) {
    // Live plugin display text — exact, no quantization.
    if (info.displayText) {
        auto text = info.displayText->format(realValue);
        if (text.isNotEmpty())
            return text;
    }

    // Sampled value table fallback (from ParameterConfigDialog or legacy).
    if (!info.valueTable.empty()) {
        int idx =
            juce::jlimit(0, static_cast<int>(info.valueTable.size()) - 1,
                         static_cast<int>(std::round(realValue * (info.valueTable.size() - 1))));
        auto text = info.valueTable[static_cast<size_t>(idx)].trim();
        if (text.isNotEmpty())
            return text;
    }

    // Discrete/Boolean bypass displayFormat — choice names / "On"/"Off".
    switch (info.scale) {
        case ParameterScale::Discrete:
            return getChoiceString(static_cast<int>(std::round(realValue)), info);
        case ParameterScale::Boolean:
            return realValue >= 0.5f ? "On" : "Off";
        default:
            break;
    }

    switch (info.displayFormat) {
        case DisplayFormat::Decibels:
            return formatDecibels(realValue, decimalPlaces);
        case DisplayFormat::Pan:
            return formatPan(realValue);
        case DisplayFormat::Percent:
            // Stored as 0..100 by convention (matches ParameterPresets::percent).
            // No scaling; the formatter just tacks on "%".
            return juce::String(realValue, decimalPlaces) + "%";
        case DisplayFormat::MidiNote:
            return formatMidiNote(realValue);
        case DisplayFormat::Beats:
            return juce::String(realValue, 2) + " beats";
        case DisplayFormat::BarsBeats:
            return formatBars(realValue);
        case DisplayFormat::Default:
            break;
    }

    // Default: dispatch on unit.
    if (info.unit == "Hz")
        return formatHz(realValue, decimalPlaces);
    if (info.unit == "ms")
        return formatMs(realValue, decimalPlaces);
    if (info.unit == "%")
        return juce::String(realValue, decimalPlaces) + "%";
    if (info.unit == "dB")
        return formatDecibels(realValue, decimalPlaces);
    if (info.unit == "st") {
        juce::String sign = realValue > 0.0f ? "+" : "";
        return sign + juce::String(realValue, decimalPlaces) + " st";
    }
    if (info.unit.isNotEmpty())
        return juce::String(realValue, decimalPlaces) + " " + info.unit;
    return juce::String(realValue, decimalPlaces);
}

std::optional<float> parseValue(const juce::String& text, const ParameterInfo& info) {
    auto trimmed = text.trim();
    if (trimmed.isEmpty())
        return std::nullopt;

    // Discrete/Boolean first — bypass displayFormat.
    if (info.scale == ParameterScale::Discrete) {
        // Match choice name (case-insensitive); fallback to index number.
        for (size_t i = 0; i < info.choices.size(); ++i) {
            if (info.choices[i].equalsIgnoreCase(trimmed))
                return static_cast<float>(i);
        }
        if (trimmed.containsOnly("0123456789-"))
            return static_cast<float>(trimmed.getIntValue());
        return std::nullopt;
    }
    if (info.scale == ParameterScale::Boolean) {
        auto lower = trimmed.toLowerCase();
        if (lower == "on" || lower == "true" || lower == "1")
            return 1.0f;
        if (lower == "off" || lower == "false" || lower == "0")
            return 0.0f;
        return std::nullopt;
    }

    auto clamp = [&](std::optional<float> v) -> std::optional<float> {
        if (!v || !std::isfinite(*v))
            return v;  // keep +/-inf for Decibels "-inf" path; caller clamps on setValue
        return juce::jlimit(info.minValue, info.maxValue, *v);
    };

    switch (info.displayFormat) {
        case DisplayFormat::Decibels:
            return clamp(parseDecibels(trimmed));
        case DisplayFormat::Pan:
            return clamp(parsePan(trimmed));
        case DisplayFormat::Percent: {
            auto t = trimmed;
            if (t.endsWith("%"))
                t = t.dropLastCharacters(1).trim();
            if (t.isEmpty())
                return std::nullopt;
            return clamp(static_cast<float>(t.getDoubleValue()));
        }
        case DisplayFormat::MidiNote:
            return clamp(parseMidiNote(trimmed));
        case DisplayFormat::Beats: {
            auto t = trimmed.toLowerCase();
            if (t.endsWith("beats"))
                t = t.dropLastCharacters(5).trim();
            if (t.isEmpty())
                return std::nullopt;
            return clamp(static_cast<float>(t.getDoubleValue()));
        }
        case DisplayFormat::BarsBeats: {
            // "bars.beats.ticks", 1-indexed for bars and beats.
            constexpr int TICKS_PER_BEAT = 480;
            constexpr int beatsPerBar = 4;
            auto parts = juce::StringArray::fromTokens(trimmed, ".", "");
            if (parts.isEmpty())
                return std::nullopt;
            int bar = parts[0].getIntValue() - 1;
            int beat = parts.size() > 1 ? parts[1].getIntValue() - 1 : 0;
            int ticks = parts.size() > 2 ? parts[2].getIntValue() : 0;
            if (bar < 0 || beat < 0 || ticks < 0)
                return std::nullopt;
            return clamp(static_cast<float>(bar * beatsPerBar + beat +
                                            ticks / static_cast<double>(TICKS_PER_BEAT)));
        }
        case DisplayFormat::Default:
            break;
    }

    // Default: dispatch on unit.
    if (info.unit == "Hz")
        return clamp(parseHz(trimmed));
    if (info.unit == "ms")
        return clamp(parseMs(trimmed));
    if (info.unit == "dB")
        return clamp(parseDecibels(trimmed));
    if (info.unit == "%") {
        auto t = trimmed;
        if (t.endsWith("%"))
            t = t.dropLastCharacters(1).trim();
        if (t.isEmpty())
            return std::nullopt;
        return clamp(static_cast<float>(t.getDoubleValue()));
    }
    if (info.unit == "st") {
        auto t = trimmed.toLowerCase();
        if (t.endsWith("st"))
            t = t.dropLastCharacters(2).trim();
        if (t.isEmpty())
            return std::nullopt;
        return clamp(static_cast<float>(t.getDoubleValue()));
    }
    // Strip matching unit suffix if any, then a bare number.
    auto t = trimmed;
    if (info.unit.isNotEmpty() && t.endsWithIgnoreCase(info.unit))
        t = t.dropLastCharacters(info.unit.length()).trim();
    if (t.isEmpty())
        return std::nullopt;
    // Reject input that is only symbols / letters.
    if (!t.containsAnyOf("0123456789."))
        return std::nullopt;
    return clamp(static_cast<float>(t.getDoubleValue()));
}

double snapNormalizedToGrid(double normalized, const ParameterInfo& info) {
    // Build the set of "natural" grid values in normalized space for this
    // parameter, then snap to the closest one. Values mirror what the
    // curve editor and track header paint as grid lines.
    std::vector<double> gridNorms;

    switch (info.scale) {
        case ParameterScale::FaderDB: {
            // dB ticks — same set the paint code uses.
            static constexpr double kDbTicks[] = {6.0,   3.0,   0.0,   -6.0,  -12.0,
                                                  -18.0, -24.0, -36.0, -48.0, -60.0};
            for (double db : kDbTicks) {
                float n = realToNormalized(static_cast<float>(db), info);
                gridNorms.push_back(static_cast<double>(n));
            }
            // Always include the endpoints so snap can reach max/min.
            gridNorms.push_back(0.0);
            gridNorms.push_back(1.0);
            break;
        }

        case ParameterScale::Discrete: {
            if (info.choices.empty())
                return normalized;
            int count = static_cast<int>(info.choices.size());
            for (int i = 0; i < count; ++i) {
                gridNorms.push_back(static_cast<double>(i) / (count - 1));
            }
            break;
        }

        case ParameterScale::Boolean:
            return normalized >= 0.5 ? 1.0 : 0.0;

        case ParameterScale::Linear: {
            // Pan parameter (-1..+1) gets L/50L/C/50R/R ticks.
            // Detect by range; otherwise fall through to 10% steps.
            if (info.minValue == -1.0f && info.maxValue == 1.0f) {
                static constexpr double kPanTicks[] = {-1.0, -0.5, 0.0, 0.5, 1.0};
                for (double p : kPanTicks) {
                    float n = realToNormalized(static_cast<float>(p), info);
                    gridNorms.push_back(static_cast<double>(n));
                }
                break;
            }
            // Generic linear (e.g., percent): 10% steps.
            for (int i = 0; i <= 10; ++i)
                gridNorms.push_back(i / 10.0);
            break;
        }

        default:
            // Generic fallback: 10% steps in normalized space.
            for (int i = 0; i <= 10; ++i)
                gridNorms.push_back(i / 10.0);
            break;
    }

    if (gridNorms.empty())
        return normalized;

    double best = gridNorms.front();
    double bestDist = std::abs(normalized - best);
    for (double g : gridNorms) {
        double d = std::abs(normalized - g);
        if (d < bestDist) {
            bestDist = d;
            best = g;
        }
    }
    return best;
}

juce::String getChoiceString(int index, const ParameterInfo& info) {
    if (info.choices.empty()) {
        return juce::String(index);
    }

    if (index >= 0 && index < static_cast<int>(info.choices.size())) {
        return info.choices[static_cast<size_t>(index)];
    }

    return juce::String(index);
}

}  // namespace ParameterUtils
}  // namespace magda
