#include "faust_agent.hpp"

#include "../daw/core/Config.hpp"
#include "llm_client_factory.hpp"
#include "llm_presets.hpp"

namespace magda {

const char* FaustAgent::getSystemPrompt() {
    return R"PROMPT(You are a Faust DSP author. Given a user description of an audio effect or
instrument, output a single JSON object describing a Faust program. Output
ONLY the JSON object — no prose, no markdown fences.

OUTPUT SCHEMA:
{
  "name": "<2-4 word name, Title Case>",
  "description": "<one short sentence describing the sound/processor>",
  "source": "<a complete, valid Faust program>"
}

SOURCE RULES — VERY IMPORTANT:
- Start with: import("stdfaust.lib");
- For an EFFECT, define: process = ... : ... ;  taking 2 inputs and returning
  2 outputs (stereo in / stereo out). Use _,_ for an unprocessed channel.
- For an INSTRUMENT/SYNTH, define: process = ... ;  with 0 inputs and 2
  outputs. Avoid this unless the user clearly asks for a synth.
- Expose user-facing controls with hslider("Label", init, min, max, step) or
  vslider(...). Keep it to AT MOST 6 sliders. Sliders must use unique
  human-readable labels — these become the parameter names in MAGDA.
- Do NOT use buttons, checkboxes, or bargraphs (the host UI ignores them).
- Do NOT use soundfile() or any external sample loading.
- Use only functions from stdfaust.lib (the standard library is bundled).
  Common picks: fi.lowpass / fi.highpass / fi.peak_eq, ef.cubicnl,
  re.zita_rev1_stereo, de.delay, os.osc, en.adsr, ba.beat.
- The full program MUST compile in the libfaust interpreter backend on its
  own, with no extra imports.

GUIDELINES:
- Stay musically useful: ranges should be tasteful (e.g. cutoff 100..8000
  Hz, drive 0..10, mix 0..1), not extreme.
- Default values should produce an audibly active starting point — not a
  bypass.
- Keep the source short and readable. Prefer one expression with named
  helpers over deeply nested anonymous parts.

EXAMPLES (shape only, do not echo verbatim):

User: "warm tape saturator with subtle wow"
{
  "name": "Tape Warmth",
  "description": "Soft tape-style saturator with gentle wow modulation.",
  "source": "import(\"stdfaust.lib\");\ndrive = hslider(\"Drive\", 3, 0, 10, 0.01);\nwow = hslider(\"Wow\", 0.3, 0, 1, 0.01);\nmix = hslider(\"Mix\", 0.8, 0, 1, 0.01);\nlfo = os.osc(0.6) * 0.002 * wow;\nsat(x) = ef.cubicnl(drive/10, 0) : *(0.7);\nch = _ <: *(1.0 + lfo) : sat : _ * mix + _ * (1.0 - mix);\nprocess = ch, ch;"
}

User: "gentle plate reverb"
{
  "name": "Plate Lite",
  "description": "Short, dark plate reverb.",
  "source": "import(\"stdfaust.lib\");\nsize = hslider(\"Size\", 0.5, 0, 1, 0.01);\ndamp = hslider(\"Damp\", 0.6, 0, 1, 0.01);\nmix = hslider(\"Mix\", 0.35, 0, 1, 0.01);\nwet = re.zita_rev1_stereo(0, 200, 6000, size*4 + 0.2, damp, 44100);\nprocess = _,_ <: (wet : *(mix), *(mix)), (*(1.0-mix), *(1.0-mix)) :> _,_;"
}
)PROMPT";
}

namespace {

void logFaustAgentConfig(const Config::AgentLLMConfig& agentConfig,
                         const llm::ProviderConfig& providerConfig) {
    DBG("MAGDA FaustAgent provider=" << agentConfig.provider << " model=" << agentConfig.model
                                     << " baseUrl=" << providerConfig.baseUrl);
}

void logFaustAgentResult(const FaustAgent::Result& r) {
    if (r.hasError)
        DBG("MAGDA FaustAgent ERROR: " + juce::String(r.error));
    else
        DBG("MAGDA FaustAgent OK name='" + juce::String(r.name) +
            "' src.len=" + juce::String(static_cast<int>(r.source.size())));
}

}  // namespace

FaustAgent::Result FaustAgent::parseJson(const juce::String& text) {
    Result result;
    result.rawOutput = text.toStdString();

    juce::String trimmed = text.trim();
    if (trimmed.startsWith("```")) {
        auto firstNewline = trimmed.indexOf("\n");
        if (firstNewline > 0)
            trimmed = trimmed.substring(firstNewline + 1);
        if (trimmed.endsWith("```"))
            trimmed = trimmed.dropLastCharacters(3).trim();
    }

    auto parsed = juce::JSON::parse(trimmed);
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr) {
        result.error = "response was not a JSON object";
        result.hasError = true;
        return result;
    }

    if (auto name = obj->getProperty("name"); name.isString())
        result.name = name.toString().toStdString();
    if (auto desc = obj->getProperty("description"); desc.isString())
        result.description = desc.toString().toStdString();
    if (auto src = obj->getProperty("source"); src.isString())
        result.source = src.toString().toStdString();

    if (result.source.empty()) {
        result.error = "JSON missing non-empty 'source'";
        result.hasError = true;
    }
    return result;
}

FaustAgent::Result FaustAgent::generate(const std::string& message) {
    Result result;
    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    auto agentConfig = Config::getInstance().getAgentLLMConfig(role::MUSIC);
    auto providerConfig = toLLMProviderConfig(agentConfig, "faust");
    logFaustAgentConfig(agentConfig, providerConfig);

    if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty() &&
        agentConfig.provider != provider::LLAMA_LOCAL) {
        result.error = "Faust agent API key not configured.";
        result.hasError = true;
        return result;
    }

    auto client = createLLMClient(agentConfig, "faust");

    llm::Request request;
    request.systemPrompt = juce::String::fromUTF8(getSystemPrompt());
    request.userMessage = juce::String::fromUTF8(message.c_str());
    request.temperature = 0.3f;

    auto response = client->sendRequest(request);
    if (!response.success) {
        result.error = response.error.toStdString();
        result.hasError = true;
        return result;
    }

    result = parseJson(response.text.trim());
    logFaustAgentResult(result);
    return result;
}

FaustAgent::Result FaustAgent::generateStreaming(const std::string& message,
                                                 TokenCallback onToken) {
    Result result;
    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    auto agentConfig = Config::getInstance().getAgentLLMConfig(role::MUSIC);
    auto providerConfig = toLLMProviderConfig(agentConfig, "faust");
    logFaustAgentConfig(agentConfig, providerConfig);

    if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty() &&
        agentConfig.provider != provider::LLAMA_LOCAL) {
        result.error = "Faust agent API key not configured.";
        result.hasError = true;
        return result;
    }

    auto client = createLLMClient(agentConfig, "faust");

    llm::Request request;
    request.systemPrompt = juce::String::fromUTF8(getSystemPrompt());
    request.userMessage = juce::String::fromUTF8(message.c_str());
    request.temperature = 0.3f;

    auto response = client->sendStreamingRequest(request, [&](const juce::String& token) {
        if (shouldStop_.load())
            return false;
        if (onToken)
            return onToken(token);
        return true;
    });

    if (!response.success) {
        result.error = response.error.toStdString();
        result.hasError = true;
        return result;
    }

    result = parseJson(response.text.trim());
    logFaustAgentResult(result);
    return result;
}

}  // namespace magda
