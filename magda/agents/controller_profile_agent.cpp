#include "controller_profile_agent.hpp"

#include <juce_llm/juce_llm.h>

#include "../daw/core/Config.hpp"
#include "llm_client_factory.hpp"
#include "llm_config_utils.hpp"

namespace magda {

const char* ControllerProfileAgent::getSystemPrompt() {
    return R"PROMPT(You generate MAGDA controller profiles from natural-language descriptions.

A profile describes a physical MIDI controller and its default bindings. Output must be
a single JSON object matching the provided schema — no prose, no markdown.

Field rules:
- "id": lowercase, dot-namespaced, short, stable. Format: "<vendor>.<model_variant>".
  Example: "novation.launchkey_mini_mk4", "akai.midimix", "generic.8_knob".
- "vendor": capitalised brand (e.g. "Novation", "Akai", "Arturia", "Generic").
- "name": human-readable model name, e.g. "Launchkey Mini MK4".
- "controls": list of physical controls on the device. Each control has:
    "controlId": stable string, e.g. "knob_1", "pad_3", "fader_2", "encoder_8".
    "kind": one of "knob", "button", "encoder".
    "cc": MIDI CC number, 0-127.
    "channel": MIDI channel 1-16 (use 1 when unknown).
- "defaultBindings": sensible starter bindings that will activate when the user enables
  the profile. For each knob or encoder, bind to a device macro by adding:
    { "controlId": "<id>", "resolverKind": "focused.macro",
      "args": { "macroIndex": "<N>" } }
  where macroIndex is a string, "0" through "7" (MAGDA exposes 8 macros per focused
  device). Cycle the index if the device has more than 8 knobs. Do NOT add default
  bindings for buttons (pads, transport) — those have no sensible default yet.

If the user doesn't specify CCs or channels, pick reasonable defaults — commonly
CCs 21-28 for knobs 1-8 on channel 1 (this matches MAGDA's generic profile).

Only "focused.macro" is a valid resolverKind today. Do not invent others.)PROMPT";
}

juce::var ControllerProfileAgent::buildSchema() {
    using llm::Schema;

    auto control = Schema::object({
        {"controlId", Schema::string()},
        {"kind", Schema::oneOf({"knob", "button", "encoder"})},
        {"cc", Schema::integer()},
        {"channel", Schema::integer()},
    });

    auto argsObj = Schema::object({
        {"macroIndex", Schema::string()},
    });

    auto defaultBinding = Schema::object({
        {"controlId", Schema::string()},
        // Constrain to the resolvers that actually exist in ResolverRegistry —
        // anything else would silently fail to materialise.
        {"resolverKind", Schema::oneOf({"focused.macro"})},
        {"args", argsObj},
    });

    return Schema::object({
        {"id", Schema::string()},
        {"vendor", Schema::string()},
        {"name", Schema::string()},
        {"controls", Schema::array(control)},
        {"defaultBindings", Schema::array(defaultBinding)},
    });
}

ControllerProfileAgent::Result ControllerProfileAgent::generate(
    const std::string& description, const std::vector<std::string>& livePortNames) {
    Result result;

    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    auto agentConfig = Config::getInstance().getAgentLLMConfig(role::CONTROLLER);

    if (agentConfig.provider != provider::LLAMA_LOCAL) {
        auto providerConfig = toLLMProviderConfig(agentConfig);
        if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty()) {
            result.error = "Controller agent API key not configured";
            result.hasError = true;
            return result;
        }
    }

    auto client = createLLMClient(agentConfig, role::CONTROLLER);

    juce::String systemPrompt = juce::String::fromUTF8(getSystemPrompt());
    if (!livePortNames.empty()) {
        systemPrompt += "\n\nCurrently connected MIDI inputs:";
        for (const auto& name : livePortNames)
            systemPrompt += "\n  - " + juce::String::fromUTF8(name.c_str());
        systemPrompt += "\nIf the user's description matches one of these, prefer that exact name.";
    }

    llm::Request request;
    request.systemPrompt = systemPrompt;
    request.userMessage = juce::String::fromUTF8(description.c_str());
    request.temperature = 0.1f;
    request.schema = buildSchema();

    auto response = client->sendRequest(request);

    if (!response.success) {
        DBG("MAGDA Controller ERROR (" + client->getName() + "/" + client->getConfig().model +
            "): " + response.error);
        result.error = response.error.toStdString();
        result.hasError = true;
        return result;
    }

    result.rawJson = response.text;
    result.wallSeconds = response.wallSeconds;

    auto parsed = juce::JSON::parse(response.text);
    if (parsed.isVoid()) {
        result.error = "LLM returned invalid JSON";
        result.hasError = true;
        return result;
    }

    auto decoded = decodeControllerProfile(parsed);
    if (!decoded.has_value() || !decoded->isValid()) {
        result.error = "Generated profile failed validation";
        result.hasError = true;
        return result;
    }

    result.profile = std::move(*decoded);

    DBG("MAGDA Controller (" + client->getName() + "/" + client->getConfig().model + "): '" +
        result.profile->id + "' (" + juce::String(result.wallSeconds, 2) + "s)");

    return result;
}

}  // namespace magda
