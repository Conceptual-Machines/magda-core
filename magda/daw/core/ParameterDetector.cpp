#include "ParameterDetector.hpp"

#include <juce_events/juce_events.h>
#include <juce_llm/juce_llm.h>

#include <cmath>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "../../agents/llm_client_factory.hpp"
#include "Config.hpp"

namespace magda {
namespace {

// File-based logging for parameter detection (avoids flooding console)
juce::File getDetectorLogFile() {
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile("Library/MAGDA/param_detector.log");
}

void logDetector(const juce::String& msg) {
    static std::mutex logMutex;
    std::lock_guard<std::mutex> lock(logMutex);
    auto f = getDetectorLogFile();
    f.appendText(msg + "\n");
}

#define DETECT_LOG(msg)                                                                            \
    do {                                                                                           \
        juce::String _s;                                                                           \
        _s << msg;                                                                                 \
        logDetector(_s);                                                                           \
    } while (0)

// Strip leading numeric value from display text to extract unit suffix
// e.g. "440.00 Hz" → "Hz", "-12.5 dB" → "dB", "50%" → "%"
juce::String extractUnitFromDisplayText(const juce::String& text) {
    auto trimmed = text.trim();
    if (trimmed.isEmpty())
        return {};

    // Find where the number ends and unit begins
    int unitStart = -1;
    bool foundDigit = false;
    for (int i = 0; i < trimmed.length(); ++i) {
        auto ch = trimmed[i];
        if (ch == '-' || ch == '+' || ch == '.' || (ch >= '0' && ch <= '9') || ch == ',' ||
            ch == 0x2212 /* Unicode minus − */) {
            foundDigit = (ch >= '0' && ch <= '9') || foundDigit;
            continue;
        }
        if (ch == ' ' && !foundDigit)
            continue;
        unitStart = i;
        break;
    }

    if (unitStart < 0 || !foundDigit)
        return {};

    return trimmed.substring(unitStart).trim();
}

// Try to parse a numeric value from display text
// e.g. "440.00 Hz" → 440.0, "-12.5 dB" → -12.5
bool parseNumberFromDisplayText(const juce::String& text, float& outValue) {
    auto trimmed = text.trim();
    if (trimmed.isEmpty())
        return false;

    // Handle special values
    auto lower = trimmed.toLowerCase();
    if (lower.startsWith("-inf") || lower.startsWith("\xe2\x88\x92inf") ||
        lower.contains("-\xe2\x88\x9e") || lower.contains("-inf")) {
        outValue = -std::numeric_limits<float>::infinity();
        return true;
    }
    if (lower.startsWith("inf") || lower.startsWith("+inf")) {
        outValue = std::numeric_limits<float>::infinity();
        return true;
    }

    // Extract numeric prefix
    juce::String numStr;
    for (int i = 0; i < trimmed.length(); ++i) {
        auto ch = trimmed[i];
        if (ch == '-' || ch == '+' || ch == '.' || (ch >= '0' && ch <= '9') ||
            ch == 0x2212 /* Unicode minus − */) {
            numStr += (ch == 0x2212) ? '-' : static_cast<char>(ch);
        } else if (ch == ',' && i + 1 < trimmed.length() && trimmed[i + 1] >= '0' &&
                   trimmed[i + 1] <= '9') {
            // Skip thousands separator
            continue;
        } else if (ch == ' ' && numStr.isEmpty()) {
            continue;
        } else {
            break;
        }
    }

    if (numStr.isEmpty() || numStr == "-" || numStr == "+" || numStr == ".")
        return false;

    outValue = numStr.getFloatValue();
    return true;
}

// Check if text looks like a boolean value
bool isBooleanText(const juce::String& text) {
    auto lower = text.toLowerCase().trim();
    return lower == "on" || lower == "off" || lower == "yes" || lower == "no" || lower == "true" ||
           lower == "false" || lower == "0" || lower == "1" || lower == "enabled" ||
           lower == "disabled";
}

// Check if text is entirely non-numeric (likely a discrete label)
bool isNonNumericText(const juce::String& text) {
    auto trimmed = text.trim();
    if (trimmed.isEmpty())
        return false;
    for (int i = 0; i < trimmed.length(); ++i) {
        auto ch = trimmed[i];
        if (ch >= '0' && ch <= '9')
            return false;
        if (ch == '.' || ch == '-' || ch == '+')
            return false;
    }
    return true;
}

// Check if text looks like a discrete label — starts with a letter
// (not a number/sign). e.g. "MG Low 6", "BN 12", "Flg HL6-", "Allpasses"
// but NOT "-Inf dB", "-12.1 dB", "3.0 dB" which start with digits/signs.
bool isLabelText(const juce::String& text) {
    auto trimmed = text.trim();
    if (trimmed.isEmpty())
        return false;
    auto first = trimmed[0];
    return (first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z');
}

// Normalise unit string to canonical form
juce::String normaliseUnit(const juce::String& unit) {
    auto lower = unit.toLowerCase().trim();
    if (lower == "hz" || lower == "hertz")
        return "Hz";
    if (lower == "khz" || lower == "kilohertz")
        return "kHz";
    if (lower == "db" || lower == "decibels" || lower == "decibel")
        return "dB";
    if (lower == "ms" || lower == "milliseconds" || lower == "msec")
        return "ms";
    if (lower == "s" || lower == "sec" || lower == "seconds")
        return "sec";
    if (lower == "%" || lower == "percent" || lower == "pct")
        return "%";
    if (lower == "st" || lower == "semitones" || lower == "semitone")
        return "semitones";
    if (lower == "ct" || lower == "cents" || lower == "cent")
        return "cents";
    if (lower == "bpm")
        return "BPM";
    // Unrecognised label — return empty so callers fall through to other heuristics
    return {};
}

// Try to parse a number from bracketed content matching a target unit.
// e.g. "50% [-9.0 dB]" with targetUnit="dB" → -9.0
// e.g. "  0% [-Inf dB]" with targetUnit="dB" → -inf
bool parseNumberFromBracketedUnit(const juce::String& text, const juce::String& targetUnit,
                                  float& outValue) {
    auto bracketStart = text.indexOf("[");
    auto bracketEnd = text.indexOf("]");
    if (bracketStart < 0 || bracketEnd <= bracketStart)
        return false;

    auto inner = text.substring(bracketStart + 1, bracketEnd).trim();
    // Check the inner text contains the target unit (case-insensitive)
    if (!inner.containsIgnoreCase(targetUnit))
        return false;

    return parseNumberFromDisplayText(inner, outValue);
}

// Infer unit from parameter name
juce::String inferUnitFromName(const juce::String& name) {
    auto lower = name.toLowerCase();
    if (lower.contains("freq") || lower.contains("cutoff") || lower == "fc")
        return "Hz";
    if (lower.contains("attack") || lower.contains("release") || lower.contains("decay") ||
        lower.contains("delay") || lower.contains("time") || lower.contains("predelay"))
        return "ms";
    if (lower.contains("gain") || lower.contains("vol") || lower.contains("level") ||
        lower.contains("output") || lower.contains("input") || lower.contains("threshold") ||
        lower.contains("makeup"))
        return "dB";
    if (lower.contains("mix") || lower.contains("wet") || lower.contains("dry") ||
        lower.contains("blend") || lower.contains("width") || lower.contains("feedback") ||
        lower.contains("drive") || lower.contains("resonance") || lower.contains("depth") ||
        lower.contains("amount") || lower.contains("intensity"))
        return "%";
    if (lower.contains("tune") || lower.contains("detune") || lower.contains("transpose") ||
        lower.contains("semitone") || lower.contains("pitch"))
        return "semitones";
    if (lower.contains("tempo") || lower.contains("bpm"))
        return "BPM";
    if (lower.contains("pan"))
        return "%";
    return {};
}

// Detect scale from sampled numeric values
ParameterScale detectScaleFromSamples(const std::vector<float>& values) {
    if (values.size() < 3)
        return ParameterScale::Linear;

    // Check for constant differences (linear)
    // Check for constant ratios (logarithmic)
    std::vector<float> diffs;
    std::vector<float> ratios;

    for (size_t i = 1; i < values.size(); ++i) {
        diffs.push_back(values[i] - values[i - 1]);
        if (values[i - 1] > 0.0001f && values[i] > 0.0001f)
            ratios.push_back(values[i] / values[i - 1]);
    }

    // Check if diffs are roughly constant (linear)
    if (!diffs.empty()) {
        float avgDiff = 0.0f;
        for (auto d : diffs)
            avgDiff += d;
        avgDiff /= static_cast<float>(diffs.size());

        if (std::abs(avgDiff) > 0.001f) {
            bool isLinear = true;
            for (auto d : diffs) {
                if (std::abs(d - avgDiff) > std::abs(avgDiff) * 0.15f) {
                    isLinear = false;
                    break;
                }
            }
            if (isLinear)
                return ParameterScale::Linear;
        }
    }

    // Check if ratios are roughly constant (logarithmic)
    if (ratios.size() >= 2) {
        float avgRatio = 0.0f;
        for (auto r : ratios)
            avgRatio += r;
        avgRatio /= static_cast<float>(ratios.size());

        if (avgRatio > 1.01f) {
            bool isLog = true;
            for (auto r : ratios) {
                if (std::abs(r - avgRatio) > avgRatio * 0.15f) {
                    isLog = false;
                    break;
                }
            }
            if (isLog)
                return ParameterScale::Logarithmic;
        }
    }

    return ParameterScale::Linear;
}

DetectedParameterInfo detectSingleParameter(const ParameterScanInput& input) {
    DetectedParameterInfo result;
    result.paramIndex = input.paramIndex;

    // 1. Boolean detection
    if (input.stateCount == 2 ||
        (input.displayTexts.size() == 2 && isBooleanText(input.displayTexts[0]) &&
         isBooleanText(input.displayTexts[1]))) {
        result.scale = ParameterScale::Boolean;
        result.minValue = 0.0f;
        result.maxValue = 1.0f;
        result.modulatable = false;
        result.confidence = 0.95f;
        return result;
    }

    // Also check boolean from display texts even if stateCount isn't 2
    if (input.displayTexts.size() >= 2) {
        bool allBoolean = true;
        for (const auto& text : input.displayTexts) {
            if (!isBooleanText(text)) {
                allBoolean = false;
                break;
            }
        }
        // But only if we see exactly 2 unique values
        if (allBoolean) {
            std::set<juce::String> unique;
            for (const auto& t : input.displayTexts)
                unique.insert(t.toLowerCase().trim());
            if (unique.size() <= 2) {
                result.scale = ParameterScale::Boolean;
                result.minValue = 0.0f;
                result.maxValue = 1.0f;
                result.modulatable = false;
                result.confidence = 0.9f;
                return result;
            }
        }
    }

    // 2. Discrete detection — state count or all non-numeric display texts
    if (input.stateCount > 0 && input.stateCount <= 1000) {
        result.scale = ParameterScale::Discrete;
        result.minValue = 0.0f;
        result.maxValue = static_cast<float>(input.stateCount - 1);
        result.modulatable = false;
        // Harvest choice labels
        for (const auto& text : input.displayTexts) {
            if (!text.isEmpty())
                result.choices.push_back(text);
        }
        result.confidence = 0.9f;
        return result;
    }

    // Check if all display texts are non-numeric → discrete
    if (input.displayTexts.size() >= 3) {
        bool allNonNumeric = true;
        for (const auto& text : input.displayTexts) {
            if (!isNonNumericText(text)) {
                allNonNumeric = false;
                break;
            }
        }
        if (allNonNumeric) {
            result.scale = ParameterScale::Discrete;
            result.minValue = 0.0f;
            result.maxValue = static_cast<float>(input.displayTexts.size() - 1);
            result.modulatable = false;
            for (const auto& text : input.displayTexts)
                result.choices.push_back(text);
            result.confidence = 0.85f;
            return result;
        }

        // Also check if all display texts are label-like (contain letters)
        // even if they also contain digits — e.g. "MG Low 6", "BN 12"
        bool allLabels = true;
        for (const auto& text : input.displayTexts) {
            if (!isLabelText(text)) {
                allLabels = false;
                break;
            }
        }
        if (allLabels) {
            result.scale = ParameterScale::Discrete;
            result.minValue = 0.0f;
            result.maxValue = static_cast<float>(input.displayTexts.size() - 1);
            result.modulatable = false;
            for (const auto& text : input.displayTexts)
                result.choices.push_back(text);
            result.confidence = 0.85f;
            return result;
        }
    }

    // 3. Continuous parameter — try to extract a real unit from display text
    juce::String displayUnit;
    if (input.displayTexts.size() >= 3) {
        auto midIndex = input.displayTexts.size() / 2;
        auto midText = input.displayTexts[midIndex];
        auto rawUnit = extractUnitFromDisplayText(midText);
        displayUnit = normaliseUnit(rawUnit);
        DETECT_LOG("  [detect] '" << input.name << "' midText='" << midText << "' rawUnit='"
                                  << rawUnit << "' displayUnit='" << displayUnit << "'");
    }
    if (displayUnit.isEmpty() && input.label.isNotEmpty()) {
        displayUnit = normaliseUnit(input.label);
        DETECT_LOG("  [detect] '" << input.name << "' using label unit='" << displayUnit << "'");
    }

    // If display text gives us a real unit (not %), parse range deterministically
    if (displayUnit.isNotEmpty() && displayUnit != "%") {
        result.unit = displayUnit;

        // Handle kHz → Hz
        if (result.unit == "kHz")
            result.unit = "Hz";

        // Parse numeric values from display texts
        std::vector<float> parsedValues;
        for (const auto& text : input.displayTexts) {
            float val;
            // Try bracketed value first: "50% [-9.0 dB]" → -9.0
            if (parseNumberFromBracketedUnit(text, displayUnit, val)) {
                parsedValues.push_back(val);
                DETECT_LOG("  [detect] '" << input.name << "' bracket-parsed '" << text << "' -> "
                                          << val);
            } else if (parseNumberFromDisplayText(text, val)) {
                auto unitSuffix = normaliseUnit(extractUnitFromDisplayText(text));
                if (unitSuffix == "kHz")
                    val *= 1000.0f;
                parsedValues.push_back(val);
                DETECT_LOG("  [detect] '" << input.name << "' parsed '" << text << "' -> " << val);
            } else {
                DETECT_LOG("  [detect] '" << input.name << "' FAILED to parse '" << text << "'");
            }
        }

        if (parsedValues.size() >= 2) {
            result.minValue = parsedValues.front();
            result.maxValue = parsedValues.back();

            bool allPositive = true;
            for (auto v : parsedValues)
                if (v <= 0.0f)
                    allPositive = false;

            if (allPositive && parsedValues.size() >= 3)
                result.scale = detectScaleFromSamples(parsedValues);

            // Scale heuristics from unit
            if (result.scale == ParameterScale::Linear) {
                if (result.unit == "Hz" && result.maxValue > result.minValue * 10.0f)
                    result.scale = ParameterScale::Logarithmic;
                else if (result.unit == "ms" && result.maxValue > result.minValue * 10.0f)
                    result.scale = ParameterScale::Logarithmic;
            }

            result.bipolar = result.minValue < 0.0f && result.maxValue > 0.0f;
        }

        result.confidence = 1.0f;  // resolved
        return result;
    }

    // 4. Display text says "%" or nothing — try name-based inference
    auto nameUnit = inferUnitFromName(input.name);
    DETECT_LOG("  [detect] '" << input.name << "' nameUnit='" << nameUnit << "' (displayUnit was '"
                              << displayUnit << "')");
    if (nameUnit.isNotEmpty()) {
        result.unit = nameUnit;

        // Try to parse real values from display text — but only if display text
        // units are compatible with the inferred unit (not showing "%" when we
        // inferred "dB", for example)
        bool displayTextsCompatible = true;
        if (input.displayTexts.size() >= 3) {
            auto midIdx = input.displayTexts.size() / 2;
            auto dtUnit = normaliseUnit(extractUnitFromDisplayText(input.displayTexts[midIdx]));
            DETECT_LOG("  [detect] '" << input.name << "' name-inferred path: dtUnit='" << dtUnit
                                      << "' nameUnit='" << nameUnit
                                      << "' compatible=" << (int)displayTextsCompatible);
            if (dtUnit.isNotEmpty() && dtUnit != nameUnit)
                displayTextsCompatible = false;
        }

        if (displayTextsCompatible) {
            std::vector<float> parsedValues;
            for (const auto& text : input.displayTexts) {
                float val;
                // Try bracketed value first: "50% [-9.0 dB]" → -9.0
                if (parseNumberFromBracketedUnit(text, nameUnit, val)) {
                    parsedValues.push_back(val);
                    DETECT_LOG("  [detect] '" << input.name << "' name-path bracket-parsed '"
                                              << text << "' -> " << val);
                } else if (parseNumberFromDisplayText(text, val)) {
                    auto unitSuffix = normaliseUnit(extractUnitFromDisplayText(text));
                    if (unitSuffix == "kHz")
                        val *= 1000.0f;
                    parsedValues.push_back(val);
                    DETECT_LOG("  [detect] '" << input.name << "' name-path parsed '" << text
                                              << "' -> " << val);
                } else {
                    DETECT_LOG("  [detect] '" << input.name << "' name-path FAILED to parse '"
                                              << text << "'");
                }
            }

            if (parsedValues.size() >= 2) {
                result.minValue = parsedValues.front();
                result.maxValue = parsedValues.back();
                result.bipolar = result.minValue < 0.0f && result.maxValue > 0.0f;

                bool allPositive = true;
                for (auto v : parsedValues)
                    if (v <= 0.0f)
                        allPositive = false;

                if (allPositive && parsedValues.size() >= 3)
                    result.scale = detectScaleFromSamples(parsedValues);

                if (result.scale == ParameterScale::Linear) {
                    if (result.unit == "Hz" && result.maxValue > result.minValue * 10.0f)
                        result.scale = ParameterScale::Logarithmic;
                    else if (result.unit == "ms" && result.maxValue > result.minValue * 10.0f)
                        result.scale = ParameterScale::Logarithmic;
                }
            }
        }

        // If we couldn't parse real values, send to AI for range refinement
        // but keep the unit from name inference
        if (result.minValue == 0.0f && result.maxValue == 0.0f) {
            result.minValue = input.rangeMin;
            result.maxValue = input.rangeMax;
            DETECT_LOG("  [detect] '" << input.name << "' name-path fallback to input range ["
                                      << input.rangeMin << "," << input.rangeMax << "]");
        }

        DETECT_LOG("  [detect] '" << input.name << "' RESOLVED via name: unit=" << result.unit
                                  << " range=[" << result.minValue << "," << result.maxValue
                                  << "]");
        result.confidence = 1.0f;  // resolved from name
        return result;
    }

    // 5. No clear unit from display text or name — send to AI for refinement.
    // Default to "%" in case AI doesn't reach this param.
    DETECT_LOG("  [detect] '" << input.name << "' AMBIGUOUS — no unit from display or name");
    result.unit = "%";
    result.confidence = 0.0f;
    result.minValue = input.rangeMin;
    result.maxValue = input.rangeMax;
    return result;
}

ParameterScale stringToScale(const juce::String& str) {
    if (str == "logarithmic")
        return ParameterScale::Logarithmic;
    if (str == "exponential")
        return ParameterScale::Exponential;
    if (str == "discrete")
        return ParameterScale::Discrete;
    if (str == "boolean")
        return ParameterScale::Boolean;
    if (str == "fader_db")
        return ParameterScale::FaderDB;
    return ParameterScale::Linear;
}

// Pairs an ambiguous parameter's scan input with its index in the results vector
struct AmbiguousParam {
    size_t originalIndex;
    ParameterScanInput input;
};

}  // anonymous namespace

namespace ParameterDetector {

std::vector<DetectedParameterInfo> detect(const std::vector<ParameterScanInput>& params) {
    // Clear log file at start of each detection run
    getDetectorLogFile().replaceWithText("=== Parameter Detection Run ===\n");
    DETECT_LOG("Detecting " << (int)params.size() << " parameters");

    std::vector<DetectedParameterInfo> results;
    results.reserve(params.size());
    for (const auto& input : params) {
        results.push_back(detectSingleParameter(input));
    }
    return results;
}

// Helper: build JSON payload for a batch of ambiguous params
juce::String buildBatchPayload(const juce::String& pluginName,
                               const std::vector<AmbiguousParam>& batch) {
    juce::Array<juce::var> paramsList;
    for (const auto& ap : batch) {
        auto* paramObj = new juce::DynamicObject();
        paramObj->setProperty("paramIndex", ap.input.paramIndex);
        paramObj->setProperty("name", ap.input.name);
        paramObj->setProperty("label", ap.input.label);
        paramObj->setProperty("rangeMin", ap.input.rangeMin);
        paramObj->setProperty("rangeMax", ap.input.rangeMax);
        paramObj->setProperty("stateCount", ap.input.stateCount);

        juce::Array<juce::var> texts;
        const auto& allTexts = ap.input.displayTexts;
        if (allTexts.size() <= 7) {
            for (const auto& t : allTexts)
                texts.add(t);
        } else {
            for (int j = 0; j < 5; ++j)
                texts.add(allTexts[static_cast<size_t>(j)]);
            texts.add("...");
            texts.add(allTexts.back());
        }
        paramObj->setProperty("displayTexts", texts);
        paramsList.add(juce::var(paramObj));
    }
    return "Plugin: " + pluginName + "\n\nParameters to classify:\n" +
           juce::JSON::toString(juce::var(paramsList), true);
}

// Helper: parse AI response and apply to results.
// Returns the number of params successfully parsed.
int parseAIResponse(const juce::String& responseText, const std::vector<AmbiguousParam>& batch,
                    std::vector<DetectedParameterInfo>& results, std::mutex& resultsMutex) {
    auto text = responseText.trim();
    // Strip markdown code fences
    if (text.startsWith("```")) {
        auto nl = text.indexOf("\n");
        if (nl >= 0)
            text = text.substring(nl + 1);
        if (text.endsWith("```"))
            text = text.substring(0, text.length() - 3).trim();
    }

    auto parsed = juce::JSON::parse(text);

    const juce::Array<juce::var>* paramsArray = nullptr;
    if (auto* obj = parsed.getDynamicObject()) {
        auto pv = obj->getProperty("parameters");
        if (pv.isArray())
            paramsArray = pv.getArray();
    } else if (parsed.isArray()) {
        paramsArray = parsed.getArray();
    }

    if (!paramsArray) {
        DBG("AI batch - failed to parse response as JSON");
        return 0;
    }

    int parsedCount = 0;
    std::lock_guard<std::mutex> lock(resultsMutex);
    for (const auto& paramVar : *paramsArray) {
        if (auto* pObj = paramVar.getDynamicObject()) {
            int paramIndex = -1;
            if (pObj->hasProperty("paramIndex"))
                paramIndex = pObj->getProperty("paramIndex");
            else if (pObj->hasProperty("index"))
                paramIndex = pObj->getProperty("index");
            if (paramIndex < 0)
                continue;

            for (const auto& ap : batch) {
                if (ap.input.paramIndex == paramIndex) {
                    auto& result = results[ap.originalIndex];
                    result.unit = pObj->getProperty("unit").toString();
                    result.scale = stringToScale(pObj->getProperty("scale").toString());
                    result.minValue = static_cast<float>((double)pObj->getProperty("minValue"));
                    result.maxValue = static_cast<float>((double)pObj->getProperty("maxValue"));
                    result.bipolar = pObj->getProperty("bipolar");
                    result.modulatable = pObj->getProperty("modulatable");
                    result.confidence = 1.0f;
                    ++parsedCount;
                    break;
                }
            }
        }
    }
    return parsedCount;
}

void detectWithAI(const juce::String& pluginName, const std::vector<ParameterScanInput>& params,
                  const std::vector<DetectedParameterInfo>& deterministicResults,
                  float confidenceThreshold, std::shared_ptr<std::atomic<bool>> cancelFlag,
                  std::function<void(int resolved, int total)> onProgress,
                  std::function<void(std::vector<DetectedParameterInfo>)> onComplete) {
    // Collect ambiguous parameters
    std::vector<AmbiguousParam> ambiguous;
    for (size_t i = 0; i < deterministicResults.size(); ++i) {
        if (deterministicResults[i].confidence < confidenceThreshold)
            ambiguous.push_back({i, params[i]});
    }

    if (ambiguous.empty()) {
        onComplete(deterministicResults);
        return;
    }

    // Split into batches of 100 params each
    const size_t batchSize = 100;
    auto batches = std::make_shared<std::vector<std::vector<AmbiguousParam>>>();
    for (size_t i = 0; i < ambiguous.size(); i += batchSize) {
        auto end = std::min(i + batchSize, ambiguous.size());
        batches->push_back({ambiguous.begin() + static_cast<ptrdiff_t>(i),
                            ambiguous.begin() + static_cast<ptrdiff_t>(end)});
    }

    auto totalAmbiguous = static_cast<int>(ambiguous.size());
    DBG("AI parameter detection - " << totalAmbiguous << " params in " << batches->size()
                                    << " parallel batches");

    auto systemPrompt = juce::String(
        "You are an audio plugin parameter classifier for a DAW.\n"
        "For each parameter, determine:\n"
        "- unit: the physical unit (Hz, dB, ms, %, semitones, cents, BPM, sec, or empty)\n"
        "- scale: how the parameter maps from normalized 0-1 to real values:\n"
        "  - linear: value = min + norm * (max - min)\n"
        "  - logarithmic: value = min * pow(max/min, norm) — for frequency, time\n"
        "  - exponential: value = pow(norm, exp) * range — for curves\n"
        "  - discrete: integer steps with labels\n"
        "  - boolean: on/off toggle\n"
        "  - fader_db: DAW fader scale (0.75 = 0dB unity)\n"
        "- minValue/maxValue: the real parameter range in the detected unit\n"
        "- bipolar: true if range straddles zero (e.g. -24 to +24 dB EQ gain)\n"
        "- modulatable: true for continuous params, false for switches/discrete\n\n"
        "Use the displayTexts array (sampled at normalized 0.0, 0.25, 0.5, 0.75, 1.0) "
        "to infer the real range and unit.\n"
        "Common patterns: frequency params are logarithmic, time params (attack/decay/release) "
        "are often logarithmic, gain/level/volume are linear in dB, mix/wet/dry are linear %.");

    auto paramSchema = llm::Schema::object({
        {"paramIndex", llm::Schema::integer()},
        {"unit",
         llm::Schema::oneOf({"Hz", "dB", "ms", "%", "semitones", "cents", "BPM", "sec", ""})},
        {"scale", llm::Schema::oneOf(
                      {"linear", "logarithmic", "exponential", "discrete", "boolean", "fader_db"})},
        {"minValue", llm::Schema::number()},
        {"maxValue", llm::Schema::number()},
        {"bipolar", llm::Schema::boolean()},
        {"modulatable", llm::Schema::boolean()},
    });
    auto responseSchema = llm::Schema::object({
        {"parameters", llm::Schema::array(paramSchema)},
    });

    auto resultsCopy = std::make_shared<std::vector<DetectedParameterInfo>>(deterministicResults);
    auto resultsMutex = std::make_shared<std::mutex>();
    auto resolvedCount = std::make_shared<std::atomic<int>>(0);
    auto completedBatches = std::make_shared<std::atomic<int>>(0);
    auto nextBatchIdx = std::make_shared<std::atomic<int>>(0);
    auto totalBatches = static_cast<int>(batches->size());

    // Use a fixed pool of worker threads that pull batches from a shared queue
    const int maxConcurrent = std::min(5, totalBatches);
    DBG("AI parameter detection - launching " << maxConcurrent << " worker threads");

    for (int workerIdx = 0; workerIdx < maxConcurrent; ++workerIdx) {
        std::thread([batches, pluginName, systemPrompt, responseSchema, cancelFlag, onProgress,
                     onComplete, resultsCopy, resultsMutex, resolvedCount, completedBatches,
                     nextBatchIdx, totalBatches, totalAmbiguous]() {
            auto agentConfig = Config::getInstance().getAgentLLMConfig("command");
            auto client = createLLMClient(agentConfig, "parameter_detector");

            // Each worker grabs batches until none remain
            while (true) {
                if (cancelFlag && cancelFlag->load())
                    break;

                int batchIdx = nextBatchIdx->fetch_add(1);
                if (batchIdx >= totalBatches)
                    break;

                const auto& batch = (*batches)[static_cast<size_t>(batchIdx)];
                auto userMessage = buildBatchPayload(pluginName, batch);

                llm::Request request;
                request.systemPrompt = systemPrompt;
                request.userMessage = userMessage;
                request.temperature = 0.0f;
                request.maxTokens = 16384;
                request.schema = responseSchema;

                DBG("AI batch " << batchIdx << " - sending " << batch.size() << " params");

                const int maxRetries = 3;
                for (int attempt = 0; attempt < maxRetries; ++attempt) {
                    if (cancelFlag && cancelFlag->load())
                        break;

                    if (attempt > 0) {
                        DBG("AI batch " << batchIdx << " - retry " << attempt);
                        std::this_thread::sleep_for(std::chrono::milliseconds(1000 * attempt));
                    }

                    auto response = client->sendStreamingRequest(request, [&](const juce::String&) {
                        if (cancelFlag && cancelFlag->load())
                            return false;
                        return true;
                    });

                    if (cancelFlag && cancelFlag->load())
                        break;

                    if (response.success) {
                        int parsed =
                            parseAIResponse(response.text, batch, *resultsCopy, *resultsMutex);
                        DBG("AI batch " << batchIdx << " - parsed " << parsed << " params");
                        int current = resolvedCount->fetch_add(parsed) + parsed;
                        juce::MessageManager::callAsync([onProgress, current, totalAmbiguous]() {
                            if (onProgress)
                                onProgress(current, totalAmbiguous);
                        });
                        break;  // Success, move to next batch
                    }

                    DBG("AI batch " << batchIdx << " error (attempt " << attempt
                                    << "): " << response.error);
                }

                // If this was the last batch, deliver results
                if (completedBatches->fetch_add(1) + 1 == totalBatches) {
                    auto finalResults = *resultsCopy;
                    juce::MessageManager::callAsync(
                        [onComplete, results = std::move(finalResults)]() { onComplete(results); });
                }
            }
        }).detach();
    }
}

}  // namespace ParameterDetector
}  // namespace magda
