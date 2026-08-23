#include "generic_sound_design_agent.hpp"

#include <juce_events/juce_events.h>

#include <functional>
#include <map>
#include <unordered_set>
#include <vector>

#include "../daw/core/Config.hpp"
#include "core/DeviceInfo.hpp"
#include "core/ParameterInfo.hpp"
#include "core/PresetManager.hpp"
#include "core/TrackManager.hpp"
#include "core/aliases/ParamNameNormalize.hpp"
#include "llm_client_factory.hpp"
#include "llm_presets.hpp"

namespace magda {

namespace {

// ---------------------------------------------------------------------------
// Parameter snapshot (message-thread read → worker-thread prompt build)
// ---------------------------------------------------------------------------

struct ParamSnapshot {
    int index = -1;
    juce::String name;
    juce::String normalized;
    juce::String unit;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float currentValue = 0.0f;
    ParameterScale scale = ParameterScale::Linear;
    std::vector<juce::String> choices;
};

// Runs `fn` on the message thread and blocks until it finishes, polling in
// short slices so a cancel returns promptly. Returns false if cancelled or the
// hop timed out. Mirrors the apply-side hop in four_osc_apply — live device
// state must only be touched on the message thread.
bool runOnMessageThreadBlocking(std::atomic<bool>& stop, std::function<void()> fn) {
    auto& mm = *juce::MessageManager::getInstance();
    if (mm.isThisTheMessageThread()) {
        fn();
        return true;
    }
    struct State {
        juce::WaitableEvent done;
        std::function<void()> fn;
    };
    auto state = std::make_shared<State>();
    state->fn = std::move(fn);
    mm.callAsync([state]() {
        state->fn();
        state->done.signal();
    });

    constexpr int sliceMs = 50;
    constexpr int maxMs = 5000;
    for (int waited = 0; waited < maxMs; waited += sliceMs) {
        if (state->done.wait(sliceMs))
            return true;
        if (stop.load())
            return false;
    }
    return false;
}

// Trim trailing zeros so "5.000" reads as "5" and "0.250" as "0.25".
juce::String tidyNumber(float v) {
    juce::String s(v, 3);
    if (s.containsChar('.')) {
        while (s.endsWithChar('0'))
            s = s.dropLastCharacters(1);
        if (s.endsWithChar('.'))
            s = s.dropLastCharacters(1);
    }
    return s;
}

// One human-readable line per parameter for the system prompt.
juce::String describeParam(const ParamSnapshot& p) {
    juce::String line = "  " + p.name;
    if (p.unit.isNotEmpty())
        line += " [" + p.unit + "]";
    if (p.scale == ParameterScale::Discrete && !p.choices.empty()) {
        line += " options: " + p.choices[0];
        for (size_t i = 1; i < p.choices.size(); ++i)
            line += " | " + p.choices[i];
    } else {
        line += " range " + tidyNumber(p.minValue) + ".." + tidyNumber(p.maxValue);
    }
    line += " (now " + tidyNumber(p.currentValue) + ")";
    return line;
}

juce::String buildSystemPrompt(const juce::String& displayName, const juce::String& description,
                               const juce::String& categoryOverride,
                               const juce::String& customPrompt,
                               const std::vector<ParamSnapshot>& params) {
    juce::String prompt;
    prompt << "You are a sound designer for the \"" << displayName << "\" audio plugin.";
    if (description.isNotEmpty())
        prompt << " " << description;
    prompt << juce::String::fromUTF8(
        "\n\nGiven a short user description, design a single preset by choosing values "
        "for the instrument's parameters. Output ONLY a JSON object — no prose, no "
        "markdown fences.\n\n");

    prompt << "OUTPUT SCHEMA:\n"
              "{\n"
              "  \"name\": \"<2-4 word preset name>\",\n"
              "  \"description\": \"<one short sentence>\",\n"
              "  \"params\": { \"<parameter name>\": <value>, ... }\n"
              "}\n\n";

    prompt << juce::String::fromUTF8(
        "RULES:\n"
        "- Use the EXACT parameter names listed below.\n"
        "- Emit each value in the parameter's own units and within its stated range "
        "(e.g. a Hz cutoff as a real frequency, an ms time as milliseconds).\n"
        "- For parameters shown with `options:`, emit one of the listed option labels "
        "as a string (exact text).\n"
        "- Omit any parameter you want left at its current value. Only set the ones that "
        "matter for the requested sound.\n"
        "- Keep output levels sensible — do not push level/gain parameters to their "
        "maximum without reason.\n\n");

    if (categoryOverride.isNotEmpty())
        prompt << "Bias the design toward this character: " << categoryOverride << "\n\n";

    if (customPrompt.isNotEmpty())
        prompt << "PLUGIN-SPECIFIC INSTRUCTIONS:\n" << customPrompt << "\n\n";

    prompt << "PARAMETERS:\n";
    for (const auto& p : params)
        prompt << describeParam(p) << "\n";

    return prompt;
}

// Strip markdown fences ("```json ... ```") an LLM may wrap the payload in.
juce::String stripFences(const juce::String& text) {
    auto t = text.trim();
    if (t.startsWith("```")) {
        auto firstNl = t.indexOfChar('\n');
        if (firstNl >= 0)
            t = t.substring(firstNl + 1);
        auto lastFence = t.lastIndexOf("```");
        if (lastFence >= 0)
            t = t.substring(0, lastFence);
    }
    return t.trim();
}

// ---------------------------------------------------------------------------
// Apply
// ---------------------------------------------------------------------------

struct ParsedPreset {
    juce::String name;
    juce::String description;
    // Raw name → value pairs; value is a number or a choice-label string.
    std::vector<std::pair<juce::String, juce::var>> params;
};

// Resolve a discrete parameter's incoming value (choice label or index) to the
// real value the device expects. Choices are laid out evenly across the param's
// real range, matching ParameterUtils' discrete mapping.
float discreteRealValue(const ParamSnapshot& p, const juce::var& value) {
    const int count = static_cast<int>(p.choices.size());
    const float step = count > 1 ? (p.maxValue - p.minValue) / static_cast<float>(count - 1) : 0.0f;

    if (value.isString()) {
        const auto wanted = normalizeParamName(value.toString());
        for (int i = 0; i < count; ++i) {
            if (normalizeParamName(p.choices[static_cast<size_t>(i)]) == wanted)
                return p.minValue + static_cast<float>(i) * step;
        }
    }
    // Numeric fallback: treat as a real value and clamp into range.
    return juce::jlimit(p.minValue, p.maxValue, static_cast<float>(value));
}

}  // namespace

// ===========================================================================

GenericSoundDesignAgent::GenericSoundDesignAgent(juce::String pluginId, juce::String displayName,
                                                 juce::String description,
                                                 std::vector<int> includedParameterIndices,
                                                 juce::String customPrompt)
    : pluginId_(std::move(pluginId)),
      displayName_(std::move(displayName)),
      description_(std::move(description)),
      includedParameterIndices_(std::move(includedParameterIndices)),
      customPrompt_(std::move(customPrompt)) {}

juce::String GenericSoundDesignAgent::generateAndApply(const juce::String& prompt,
                                                       const ChainNodePath& path,
                                                       llm::Conversation& conversation,
                                                       TokenCallback onToken) {
    juce::ignoreUnused(conversation);  // single-shot preset design for now
    shouldStop_ = false;
    if (shouldStop_.load())
        return "cancelled";

    // --- 1. Snapshot the device's parameters on the message thread ----------
    std::vector<ParamSnapshot> params;
    juce::String deviceName = displayName_;
    bool deviceFound = false;
    const bool snapped = runOnMessageThreadBlocking(shouldStop_, [&]() {
        auto* device = TrackManager::getInstance().getDeviceInChainByPath(path);
        if (device == nullptr)
            return;
        deviceFound = true;
        if (device->name.isNotEmpty())
            deviceName = device->name;
        const std::unordered_set<int> included(includedParameterIndices_.begin(),
                                               includedParameterIndices_.end());
        params.reserve(included.empty() ? device->parameters.size() : included.size());
        for (size_t i = 0; i < device->parameters.size(); ++i) {
            if (!included.empty() && included.find(static_cast<int>(i)) == included.end())
                continue;
            const auto& info = device->parameters[i];
            ParamSnapshot snap;
            snap.index = info.paramIndex >= 0 ? info.paramIndex : static_cast<int>(i);
            snap.name = info.name;
            snap.normalized = normalizeParamName(info.name);
            snap.unit = info.unit;
            snap.minValue = info.minValue;
            snap.maxValue = info.maxValue;
            snap.currentValue = info.currentValue;
            snap.scale = info.scale;
            snap.choices = info.choices;
            if (snap.normalized.isNotEmpty())
                params.push_back(std::move(snap));
        }

        // Third-party plugins occasionally expose the same display name for
        // several automatable parameters. Give only those collisions a stable
        // index suffix so every JSON key still resolves to exactly one target.
        std::map<juce::String, int> nameCounts;
        for (const auto& param : params)
            ++nameCounts[param.normalized];
        for (auto& param : params) {
            if (nameCounts[param.normalized] <= 1)
                continue;
            param.name += " [#" + juce::String(param.index) + "]";
            param.normalized = normalizeParamName(param.name);
        }
    });

    if (!snapped)
        return shouldStop_.load() ? juce::String("cancelled")
                                  : juce::String("(device read timed out)");
    if (!deviceFound)
        return "(target device not found)";
    if (params.empty())
        return "(device exposes no parameters)";

    // --- 2. LLM call (worker thread — never blocks the message thread) ------
    auto agentConfig = Config::getInstance().getAgentLLMConfig(role::MUSIC);
    auto providerConfig = toLLMProviderConfig(agentConfig, "sound_design");
    if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty() &&
        agentConfig.provider != provider::LLAMA_LOCAL)
        return "error: sound design agent API key not configured.";

    auto client = createLLMClient(agentConfig, "sound_design");

    llm::Request request;
    request.systemPrompt =
        buildSystemPrompt(deviceName, description_, categoryOverride_, customPrompt_, params);
    request.userMessage = prompt;
    // Low temperature — sound design rewards consistency over wandering.
    request.temperature = 0.2f;

    llm::Response response;
    if (onToken) {
        response = client->sendStreamingRequest(request, [&](const juce::String& token) {
            if (shouldStop_.load())
                return false;
            return onToken(token);
        });
    } else {
        response = client->sendRequest(request);
    }

    if (shouldStop_.load())
        return "cancelled";
    if (!response.success)
        return "error: " + response.error;

    // --- 3. Parse the JSON payload ------------------------------------------
    auto parsed = juce::JSON::parse(stripFences(response.text));
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr)
        return "error: preset was not valid JSON";

    ParsedPreset preset;
    preset.name = obj->getProperty("name").toString();
    preset.description = obj->getProperty("description").toString();
    if (auto* paramsObj = obj->getProperty("params").getDynamicObject()) {
        for (const auto& kv : paramsObj->getProperties())
            preset.params.emplace_back(kv.name.toString(), kv.value);
    }
    if (preset.params.empty())
        return "error: preset contained no parameters";

    // --- 4. Apply on the message thread -------------------------------------
    std::map<juce::String, const ParamSnapshot*> byName;
    for (const auto& p : params)
        byName[p.normalized] = &p;

    int applied = 0;
    int skipped = 0;
    const bool didApply = runOnMessageThreadBlocking(shouldStop_, [&]() {
        auto& tm = TrackManager::getInstance();
        for (const auto& [rawName, value] : preset.params) {
            auto it = byName.find(normalizeParamName(rawName));
            if (it == byName.end()) {
                ++skipped;
                continue;
            }
            const ParamSnapshot& p = *it->second;
            float real;
            if (p.scale == ParameterScale::Discrete && !p.choices.empty())
                real = discreteRealValue(p, value);
            else
                real = juce::jlimit(p.minValue, p.maxValue, static_cast<float>(value));
            tm.setDeviceParameterValue(path, p.index, real);
            ++applied;
        }

        // Seed the next save dialog with the agent's preset name.
        if (preset.name.isNotEmpty()) {
            if (auto* device = tm.getDeviceInChainByPath(path))
                PresetManager::getInstance().setSuggestedPresetName(device->id, preset.name);
        }
    });

    if (!didApply)
        return shouldStop_.load() ? juce::String("cancelled") : juce::String("(apply timed out)");
    if (applied == 0)
        return "error: none of the parameters matched this device";

    juce::String status = "applied " + juce::String(applied) + " params to " + deviceName;
    if (skipped > 0)
        status += ", skipped " + juce::String(skipped);
    return status;
}

}  // namespace magda
