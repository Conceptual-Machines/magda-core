#include "gain_staging_agent.hpp"

#include <algorithm>
#include <cmath>

#include "../daw/core/Config.hpp"
#include "../daw/core/GainStagingManager.hpp"
#include "llm_client_factory.hpp"
#include "llm_config_utils.hpp"
#include "llm_presets.hpp"

namespace magda {

namespace {

// Coarse device classification from the plugin id / name, so the prompt can
// carry a "kind" the model reasons about (limiter, compressor, saturator, ...).
std::string classifyKind(const GainStagingAgent::DeviceLevel& d) {
    if (d.isInstrument)
        return "instrument";

    auto hay = juce::String(d.pluginId + " " + d.name).toLowerCase();
    auto has = [&hay](const char* s) { return hay.contains(s); };

    if (has("limiter"))
        return "limiter";
    if (has("clipper") || has("clip"))
        return "clipper";
    if (has("compressor") || has("comp"))
        return "compressor";
    if (has("satur") || has("grit") || has("drive") || has("distort") || has("bitcrush"))
        return "saturator";
    if (has("gate") || has("expander"))
        return "gate";
    if (has("eq") || has("equaliser") || has("equalizer") || has("filter"))
        return "eq";
    if (has("reverb") || has("delay") || has("chorus") || has("phaser") || has("flanger"))
        return "fx";
    return "effect";
}

juce::String stripToJsonObject(const juce::String& raw) {
    auto start = raw.indexOfChar('{');
    auto end = raw.lastIndexOfChar('}');
    if (start >= 0 && end > start)
        return raw.substring(start, end + 1);
    return raw;
}

bool isNumericVar(const juce::var& value) {
    return value.isInt() || value.isInt64() || value.isDouble();
}

bool parseDecisionIndex(const juce::var& value, int& indexOut) {
    if (!isNumericVar(value))
        return false;

    const double asDouble = static_cast<double>(value);
    const double rounded = std::round(asDouble);
    if (std::abs(asDouble - rounded) > 0.000001)
        return false;

    indexOut = static_cast<int>(rounded);
    return true;
}

}  // namespace

const char* GainStagingAgent::getSystemPrompt() {
    return "You are a mixing engineer setting the gain structure on one track's device chain.\n"
           "You are given the devices in signal order, each with: an integer id, a name, a kind, "
           "the peak level in dBFS measured at the device OUTPUT during playback, its current "
           "output trim in dB, and a 'suggestedTrimDb' -- the trim that lands this stage's output "
           "exactly at the target (the arithmetic is done for you). MAGDA devices also include "
           "their current settings (a 'settings' list of name/value/unit) so you can reason about "
           "thresholds, drive, ceilings, and makeup. You ONLY set the output trim; you do NOT "
           "change any of those settings.\n"
           "Use suggestedTrimDb as your BASELINE for each device and keep it unless you have a "
           "musical reason to deviate -- this keeps the chain on target. Deviate (and explain it "
           "in "
           "the reason) only when warranted:\n"
           "- Aim each stage's output near the target so no stage runs hot or starves the next.\n"
           "- A limiter or clipper at the END of the chain sets the ceiling: do NOT drive hard "
           "into it. Ease the level entering it back toward the target and leave its own trim near "
           "unity.\n"
           "- A saturator / distortion / grit may be driven a few dB hotter than the target for "
           "harmonic colour when it suits the chain; note this in the reason.\n"
           "- A compressor reacts to its INPUT level (threshold): set the level going into it "
           "sensibly, not only its output.\n"
           "- Transparent stages (EQ, utility) already near the target need little or no change.\n"
           "- Trims are output gains in dB, clamped to [-60, +12]. Prefer attenuation; only boost "
           "a "
           "stage that is genuinely too quiet.\n"
           "Return ONLY JSON, no prose outside it, of the form:\n"
           "{\"summary\":\"one or two sentences\",\"decisions\":[{\"id\":<int>,\"gainDb\":<number>,"
           "\"reason\":\"short\"}]}\n"
           "Include every device id exactly once.";
}

juce::String GainStagingAgent::buildUserMessage(float targetPeakDb,
                                                const std::vector<DeviceLevel>& devices) const {
    juce::Array<juce::var> arr;
    for (int i = 0; i < (int)devices.size(); ++i) {
        const auto& d = devices[(size_t)i];
        auto* obj = new juce::DynamicObject();
        obj->setProperty("id", i);  // list index (signal order), unique handle
        obj->setProperty("name", juce::String(d.name));
        obj->setProperty("kind", juce::String(classifyKind(d)));
        obj->setProperty("peakDb", juce::String(d.capturedPeakDb, 1).getDoubleValue());
        obj->setProperty("currentGainDb", juce::String(d.currentGainDb, 1).getDoubleValue());
        obj->setProperty("suggestedTrimDb", juce::String(d.suggestedGainDb, 1).getDoubleValue());

        if (!d.params.empty()) {
            juce::Array<juce::var> params;
            for (const auto& p : d.params) {
                auto* po = new juce::DynamicObject();
                po->setProperty("name", juce::String(p.name));
                po->setProperty("value", juce::String(p.value, 2).getDoubleValue());
                if (!p.unit.empty())
                    po->setProperty("unit", juce::String(p.unit));
                params.add(juce::var(po));
            }
            obj->setProperty("settings", juce::var(params));
        }
        arr.add(juce::var(obj));
    }

    juce::String msg;
    msg << "Target peak: " << juce::String(targetPeakDb, 1) << " dBFS\n";
    msg << "Devices (signal order):\n" << juce::JSON::toString(juce::var(arr));
    return msg;
}

void GainStagingAgent::parseDecisions(const juce::String& rawText,
                                      const std::vector<DeviceLevel>& devices,
                                      Result& result) const {
    result.rawOutput = rawText.toStdString();

    const int deviceCount = (int)devices.size();

    auto parsed = juce::JSON::parse(stripToJsonObject(rawText));
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr) {
        result.hasError = true;
        result.error = "Could not parse AI response as JSON.";
        return;
    }

    if (auto summary = obj->getProperty("summary"); summary.isString())
        result.summary = summary.toString().toStdString();

    auto decisions = obj->getProperty("decisions");
    std::vector<bool> seen(static_cast<size_t>(deviceCount), false);
    juce::StringArray problems;

    if (auto* arr = decisions.getArray()) {
        for (const auto& item : *arr) {
            auto* d = item.getDynamicObject();
            if (d == nullptr) {
                problems.add("decision is not an object");
                continue;
            }

            int index = -1;
            if (!d->hasProperty("id") || !parseDecisionIndex(d->getProperty("id"), index)) {
                problems.add("decision missing numeric integer id");
                continue;
            }
            if (index < 0 || index >= deviceCount) {
                problems.add("decision id out of range: " + juce::String(index));
                continue;  // ignore indices the model invented
            }
            if (seen[static_cast<size_t>(index)]) {
                problems.add("duplicate decision id: " + juce::String(index));
                continue;
            }

            const auto gainDb = d->getProperty("gainDb");
            if (!d->hasProperty("gainDb") || !isNumericVar(gainDb)) {
                problems.add("decision " + juce::String(index) + " missing numeric gainDb");
                continue;
            }

            Decision dec;
            dec.index = index;
            dec.newGainDb = juce::jlimit(kGainStageMinGainDb, kGainStageMaxGainDb,
                                         static_cast<float>(static_cast<double>(gainDb)));
            if (auto reason = d->getProperty("reason"); reason.isString())
                dec.reason = reason.toString().toStdString();
            result.decisions.push_back(dec);
            seen[static_cast<size_t>(index)] = true;
        }
    }

    for (int index = 0; index < deviceCount; ++index) {
        if (!seen[static_cast<size_t>(index)])
            problems.add("missing decision id: " + juce::String(index));
    }

    if (!problems.isEmpty()) {
        result.hasError = true;
        result.error = "AI response contained malformed decisions: " +
                       problems.joinIntoString("; ").toStdString();
        return;
    }

    if (result.decisions.empty()) {
        result.hasError = true;
        result.error = "AI response contained no usable decisions.";
    }
}

GainStagingAgent::Result GainStagingAgent::generate(float targetPeakDb,
                                                    const std::vector<DeviceLevel>& devices) {
    Result result;
    if (devices.empty()) {
        result.hasError = true;
        result.error = "No devices to stage.";
        return result;
    }

    auto agentConfig = Config::getInstance().getAgentLLMConfig(role::COMMAND);
    auto providerConfig = toLLMProviderConfig(agentConfig, "gain_staging");
    if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty() &&
        agentConfig.provider != provider::LLAMA_LOCAL) {
        result.hasError = true;
        result.error = "AI is not configured (no API key).";
        return result;
    }

    auto client = createLLMClient(agentConfig, "gain_staging");
    if (client == nullptr) {
        result.hasError = true;
        result.error = "Could not create the LLM client.";
        return result;
    }

    llm::Request request;
    request.systemPrompt = juce::String::fromUTF8(getSystemPrompt());
    request.userMessage = buildUserMessage(targetPeakDb, devices);
    request.temperature = 0.2f;

    auto response = client->sendRequest(request);
    if (!response.success) {
        result.hasError = true;
        result.error = response.error.toStdString();
        return result;
    }

    parseDecisions(response.text, devices, result);
    return result;
}

}  // namespace magda
