#include "faust_agent.hpp"

#include "../daw/core/Config.hpp"
#include "llm_client_factory.hpp"
#include "llm_presets.hpp"
#include "mcp/MCPServerManager.hpp"

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
- Expose user-facing controls. Pick the right control kind:
  * Continuous values: hslider("Label", init, min, max, step) or vslider(...)
    or nentry(...). Add [scale:log] for cutoffs / time / freqs:
      cutoff = hslider("Cutoff [unit:Hz][scale:log]", 800, 20, 20000, 0.1);
  * On / off toggles:  checkbox("Bypass")  — produces 0/1.
  * Discrete picker (dropdown): a slider with [style:menu{...}]:
      mode = hslider("Mode [style:menu{'Off':0;'Soft':1;'Hard':2}]", 0, 0, 2, 1);
- Pin every control to a stable slot index with [idx:N], 0..63 inclusive.
  Use the SAME idx for the SAME control across regenerations so the user's
  macro / mod / MIDI Learn links survive an edit:
      drive = hslider("Drive [idx:0][unit:dB]", 0, 0, 24, 0.1);
      mix   = hslider("Mix   [idx:1]",          1, 0, 1,  0.01);
- Up to 64 controls. Keep it musically tasteful; 4–8 is usually plenty.
- Do NOT use buttons (momentary), bargraphs (display-only), or soundfile()
  (no external sample loading).
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
  "source": "import(\"stdfaust.lib\");\ndrive = hslider(\"Drive [idx:0]\", 3, 0, 10, 0.01);\nwow = hslider(\"Wow [idx:1]\", 0.3, 0, 1, 0.01);\nmix = hslider(\"Mix [idx:2]\", 0.8, 0, 1, 0.01);\nlfo = os.osc(0.6) * 0.002 * wow;\nsat(x) = ef.cubicnl(drive/10, 0) : *(0.7);\nch = _ <: *(1.0 + lfo) : sat : _ * mix + _ * (1.0 - mix);\nprocess = ch, ch;"
}

User: "gentle plate reverb with mode switch"
{
  "name": "Plate Lite",
  "description": "Short, dark plate reverb with a Plate / Hall mode.",
  "source": "import(\"stdfaust.lib\");\nsize = hslider(\"Size [idx:0]\", 0.5, 0, 1, 0.01);\ndamp = hslider(\"Damp [idx:1]\", 0.6, 0, 1, 0.01);\nmix  = hslider(\"Mix  [idx:2]\", 0.35, 0, 1, 0.01);\nmode = hslider(\"Mode [idx:3][style:menu{'Plate':0;'Hall':1}]\", 0, 0, 1, 1);\nfreeze = checkbox(\"Freeze [idx:4]\");\nwetSize = size * 4 + 0.2 + mode * 1.5;\nwet = re.zita_rev1_stereo(0, 200, 6000, wetSize, damp, 44100);\nprocess = _,_ <: (wet : *(mix), *(mix)), (*(1.0-mix), *(1.0-mix)) :> _,_;"
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
    request.userMessage = buildUserMessage(message);
    request.temperature = 0.3f;

    auto response = client->sendRequest(request);
    if (!response.success) {
        result.error = response.error.toStdString();
        result.hasError = true;
        return result;
    }

    result = parseJson(response.text.trim());
    if (!result.hasError)
        result = validateWithMCP(std::move(result));
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
    request.userMessage = buildUserMessage(message);
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
    if (!result.hasError)
        result = validateWithMCP(std::move(result));
    logFaustAgentResult(result);
    return result;
}

FaustAgent::Result FaustAgent::validateWithMCP(Result result) {
    auto* mcp = MCPServerManager::getInstance().getServer("faust-mcp");
    if (mcp == nullptr) {
        lastFailedSource_.clear();
        lastCompileError_.clear();
        return result;
    }

    auto* args = new juce::DynamicObject();
    args->setProperty("code", juce::String(result.source));
    args->setProperty("name", juce::String(result.name));

    auto mcpResult = mcp->callTool("compile_faust", juce::var(args));
    if (mcpResult.success) {
        lastFailedSource_.clear();
        lastCompileError_.clear();
        return result;
    }

    DBG("MCPClient compile_faust error: " + mcpResult.error);
    lastFailedSource_ = result.source;
    lastCompileError_ = mcpResult.error.toStdString();

    result.error = "Faust compilation failed:\n" + lastCompileError_ +
                   "\n\nWould you like me to try fixing it?";
    result.hasError = true;
    return result;
}

juce::String FaustAgent::buildUserMessage(const std::string& message) const {
    if (lastFailedSource_.empty())
        return juce::String::fromUTF8(message.c_str());

    return "My previous Faust code failed to compile:\n\n```\n" + juce::String(lastFailedSource_) +
           "\n```\n\nCompiler error:\n" + juce::String(lastCompileError_) +
           "\n\nUser request: " + juce::String::fromUTF8(message.c_str()) +
           "\n\nFix the code based on the compiler error and the user's request. "
           "Output the corrected JSON object.";
}

}  // namespace magda
