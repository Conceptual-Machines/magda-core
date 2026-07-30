#include "faust_agent.hpp"

#include <array>
#include <regex>

#include "../daw/audio/plugins/FaustMetadataParser.hpp"
#include "../daw/core/Config.hpp"
#include "llm_client_factory.hpp"
#include "llm_presets.hpp"
#include "mcp/MCPServerManager.hpp"

namespace magda {

const char* FaustAgent::getSystemPrompt(Target target) {
    if (target == Target::Instrument) {
        return R"PROMPT(You are a Faust polyphonic instrument author. Given a user description,
output a single JSON object describing a complete Faust instrument program. Output ONLY the JSON
object — no prose and no markdown fences.

OUTPUT SCHEMA:
{
  "name": "<2-4 word name, Title Case>",
  "description": "<one short sentence describing the instrument>",
  "source": "<a complete, valid Faust program>"
}

POLYPHONIC INSTRUMENT RULES — REQUIRED:
- This is one MIDI voice driven by MAGDA's Faust voice allocator. Declare exactly these reserved
  controls, with these lowercase labels and NO [idx:N] metadata:
      freq = hslider("freq", 440, 20, 20000, 0.01);
      gain = hslider("gain", 0.5, 0, 1, 0.01);
      gate = button("gate");
- `freq`, `gain`, and `gate` are host-owned voice controls. Never expose them as user parameters,
  rename them, add metadata to them, or use them for another purpose.
- Shape amplitude with a MIDI-driven envelope that uses gate, for example en.adsr(..., gate), and
  multiply the voice by both that envelope and gain.
- Generate a valid mono or stereo instrument output. A mono voice may be fanned to stereo with
  `process = voice <: _, _;`.
- Every user-facing hslider, vslider, nentry, or checkbox needs a unique [idx:N] in 0..63. Do not
  add [idx:N] to freq, gain, or gate. Use 4–8 musical user controls where possible.
- Do not use soundfile() or external imports other than stdfaust.lib. Keep the full program valid
  in the bundled libfaust interpreter backend.

GUIDELINES:
- Use tasteful, playable defaults and sensible ranges.
- Use stdfaust.lib oscillators, filters, envelopes and effects only.
- Keep source short and readable with named helpers. The default must make sound after MIDI note-on.
)PROMPT";
    }
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
- Write process as a single processing chain from input to output, e.g.
      process = _ : drive : filter;
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
- For a stereo effect with a stereo audio sidechain, declare four inputs:
      process(mainL, mainR, sideL, sideR) = ...;
  MAGDA supplies the source track as the third and fourth channels.
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
  "source": "import(\"stdfaust.lib\");\ndrive = hslider(\"Drive [idx:0]\", 3, 0, 10, 0.01);\nwow = hslider(\"Wow [idx:1]\", 0.3, 0, 1, 0.01);\nmix = hslider(\"Mix [idx:2]\", 0.8, 0, 1, 0.01);\nlfo = os.osc(0.6) * 0.002 * wow;\nsat(x) = ef.cubicnl(drive/10, 0) : *(0.7);\nprocess = _ <: *(1.0 + lfo) : sat : _ * mix + _ * (1.0 - mix);"
}

User: "gentle plate reverb with mode switch"
{
  "name": "Plate Lite",
  "description": "Short, dark plate reverb with a Plate / Hall mode.",
  "source": "import(\"stdfaust.lib\");\nsize = hslider(\"Size [idx:0]\", 0.5, 0, 1, 0.01);\ndamp = hslider(\"Damp [idx:1]\", 0.6, 0, 1, 0.01);\nmix = hslider(\"Mix [idx:2]\", 0.35, 0, 1, 0.01);\nmode = hslider(\"Mode [idx:3][style:menu{'Plate':0;'Hall':1}]\", 0, 0, 1, 1);\nfreeze = checkbox(\"Freeze [idx:4]\");\nfb = (0.7 + size * 0.28 + mode * 0.02) * (1.0 - freeze) + freeze * 0.999;\nwet = re.mono_freeverb(fb, fb, damp, 0.5);\nprocess = _ <: (_ : *(1.0 - mix)), (_ : wet : *(mix)) :> _;"
}
)PROMPT";
}

namespace {

// MAGDA-side validation of the [idx:N] parameter contract, run BEFORE the Faust
// compile check. The Faust compiler treats our [...] tags as opaque label text,
// so a missing / duplicate / out-of-range idx (or >64 controls) compiles fine
// yet breaks parameter links on the next regeneration. Catch it here and feed
// failures into the same retry loop the compiler errors use.
bool validateSourceImpl(FaustAgent::Target target, const std::string& source,
                        std::string& errorOut) {
    static const std::regex controlDecl(
        R"RX((hslider|vslider|nentry|checkbox|button)\s*\(\s*"([^"]*)")RX");

    juce::StringArray problems;
    std::array<bool, 64> used{};
    int userControlCount = 0;
    bool sawFreq = false;
    bool sawGain = false;
    bool sawGate = false;

    for (auto it = std::sregex_iterator(source.begin(), source.end(), controlDecl);
         it != std::sregex_iterator(); ++it) {
        const juce::String kind((*it)[1].str());
        const juce::String rawLabel((*it)[2].str());
        const auto parsed = magda::daw::audio::parseFaustLabel(rawLabel);
        const int idx = parsed.metadata.slotIndex;
        const juce::String name =
            parsed.cleanLabel.trim().isNotEmpty() ? parsed.cleanLabel.trim() : rawLabel;
        const auto normalized = name.toLowerCase();
        const bool reserved =
            target == FaustAgent::Target::Instrument &&
            (normalized == "freq" || normalized == "gain" || normalized == "gate");

        if (reserved) {
            if (idx != -1)
                problems.add("reserved voice control \"" + name + "\" must not have [idx:N]");
            if (normalized == "freq") {
                if (sawFreq)
                    problems.add("instrument declares reserved freq more than once");
                sawFreq = true;
                if (kind == "button" || kind == "checkbox")
                    problems.add("reserved freq must be a continuous slider control");
            } else if (normalized == "gain") {
                if (sawGain)
                    problems.add("instrument declares reserved gain more than once");
                sawGain = true;
                if (kind == "button" || kind == "checkbox")
                    problems.add("reserved gain must be a continuous slider control");
            } else {
                if (sawGate)
                    problems.add("instrument declares reserved gate more than once");
                sawGate = true;
                if (kind != "button")
                    problems.add("reserved gate must be declared as button(\"gate\")");
            }
            continue;
        }

        // Preserve the effect validator's existing contract: it validates the
        // indexed controls it manages, while the effect prompt discourages
        // buttons. Buttons matter structurally only for instrument gate.
        if (target == FaustAgent::Target::Effect && kind == "button")
            continue;

        ++userControlCount;
        if (kind == "button") {
            problems.add("user control \"" + name + "\" must not be a momentary button");
            continue;
        }
        if (idx == -1)
            problems.add("control \"" + name + "\" is missing [idx:N]");
        else if (idx < 0 || idx >= 64)
            problems.add("control \"" + name + "\" has out-of-range [idx:" + juce::String(idx) +
                         "] (must be 0..63)");
        else if (used[static_cast<size_t>(idx)])
            problems.add("[idx:" + juce::String(idx) + "] is used by more than one control");
        else
            used[static_cast<size_t>(idx)] = true;
    }

    if (userControlCount > 64)
        problems.add("too many user controls (" + juce::String(userControlCount) +
                     "); the limit is 64");

    if (target == FaustAgent::Target::Instrument) {
        if (!sawFreq)
            problems.add("instrument is missing reserved freq control");
        if (!sawGain)
            problems.add("instrument is missing reserved gain control");
        if (!sawGate)
            problems.add("instrument is missing reserved gate control");
    }

    if (problems.isEmpty())
        return true;
    errorOut = problems.joinIntoString("\n").toStdString();
    return false;
}

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

bool FaustAgent::validateSource(Target target, const std::string& source, std::string& errorOut) {
    return validateSourceImpl(target, source, errorOut);
}

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

FaustAgent::Result FaustAgent::generate(const std::string& message,
                                        llm::Conversation& conversation) {
    return runConversational(message, conversation, {});
}

FaustAgent::Result FaustAgent::generateStreaming(const std::string& message,
                                                 llm::Conversation& conversation,
                                                 TokenCallback onToken) {
    return runConversational(message, conversation, std::move(onToken));
}

bool FaustAgent::compileCheck(const std::string& name, const std::string& source,
                              std::string& errorOut, bool& verified) {
    verified = false;
    auto& manager = MCPServerManager::getInstance();
    if (!manager.isServerEnabled("faust-mcp"))
        return true;  // explicitly disabled: stage for manual review, never live-apply

    auto* mcp = manager.getServer("faust-mcp");
    if (mcp == nullptr) {
        errorOut = "Faust MCP is enabled but unavailable; code was not verified.";
        return false;
    }

    auto* args = new juce::DynamicObject();
    args->setProperty("code", juce::String(source));
    args->setProperty("name", juce::String(name));

    auto mcpResult = mcp->callTool("compile_faust", juce::var(args));
    if (mcpResult.success) {
        verified = true;
        return true;
    }

    DBG("MCPClient compile_faust error: " + mcpResult.error);
    errorOut = mcpResult.error.toStdString();
    return false;
}

FaustAgent::Result FaustAgent::runConversational(const std::string& message,
                                                 llm::Conversation& conversation,
                                                 TokenCallback onToken) {
    Result result;
    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    const auto profile = Config::getInstance().getAgentInferenceConfig(role::FAUST);
    if (!profile.usesLLM()) {
        result.error = "Faust agent's configured inference backend is not supported yet.";
        result.hasError = true;
        return result;
    }
    const auto& agentConfig = profile.llm;
    auto providerConfig = toLLMProviderConfig(agentConfig, "faust");
    logFaustAgentConfig(agentConfig, providerConfig);

    if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty() &&
        agentConfig.provider != provider::LLAMA_LOCAL) {
        result.error = "Faust agent API key not configured.";
        result.hasError = true;
        return result;
    }

    auto client = createLLMClient(agentConfig, "faust");

    constexpr int kMaxAttempts = 3;

    // Snapshot the conversation as it stands. Each attempt runs against THIS
    // base, so failed attempts never chain off each other or get persisted:
    // the retry carries the broken code + error inline instead. On success we
    // commit only the original prompt + the working reply, keeping the history
    // clean (no wasted context on fix attempts) for both stateless providers
    // and the Responses API.
    const auto baseMessages = conversation.messages;
    const auto basePrevId = conversation.lastResponseId;
    const juce::String originalPrompt = juce::String::fromUTF8(message.c_str());
    juce::String userTurn = originalPrompt;
    std::string lastError;

    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        if (shouldStop_.load()) {
            result = Result{};
            result.error = "Cancelled";
            result.hasError = true;
            return result;
        }

        llm::Conversation working;
        working.messages = baseMessages;
        working.lastResponseId = basePrevId;

        llm::Request request;
        request.systemPrompt = juce::String::fromUTF8(getSystemPrompt(target_));
        request.userMessage = userTurn;
        request.temperature = 0.3f;

        llm::Response response;
        if (onToken) {
            response = client->continueConversationStreaming(
                working, request, [this, &onToken](const juce::String& token) {
                    if (shouldStop_.load())
                        return false;
                    return onToken ? onToken(token) : true;
                });
        } else {
            response = client->continueConversation(working, request);
        }

        if (!response.success) {
            result = Result{};
            result.error = response.error.toStdString();
            result.hasError = true;
            return result;
        }

        result = parseJson(response.text.trim());
        if (result.hasError) {
            // Not valid JSON — retry off the original context.
            lastError = result.error;
            userTurn = originalPrompt +
                       "\n\n(Your previous reply was not valid JSON. Output ONLY the JSON "
                       "object with fields name, description and source.)";
            continue;
        }

        // Validate our [idx:N] metadata contract before spending a compile.
        // The Faust compiler can't see these problems (the tags are opaque to
        // it), so we check them ourselves and retry on failure.
        std::string metaErr;
        if (!validateSource(target_, result.source, metaErr)) {
            lastError = metaErr;
            if (onToken)
                onToken(juce::String::fromUTF8("\n[metadata invalid, fixing...]\n"));
            userTurn = originalPrompt +
                       "\n\n(Your previous attempt had invalid control metadata:\n" + metaErr +
                       "\nFix every target-specific control requirement and output the corrected "
                       "JSON object.)";
            continue;
        }

        std::string compileErr;
        bool verified = false;
        if (compileCheck(result.name, result.source, compileErr, verified)) {
            // Success — commit the clean turns (original prompt + working reply)
            // to the persistent conversation and chain off the good response.
            conversation.messages.push_back({"user", originalPrompt});
            conversation.messages.push_back({"assistant", response.text.trim()});
            conversation.lastResponseId = working.lastResponseId;
            result.mcpVerified = verified;
            logFaustAgentResult(result);
            return result;
        }

        // Compile failed. Retry off the ORIGINAL context with the broken code
        // and the error inline, so failed attempts neither chain off each other
        // nor get persisted.
        lastError = compileErr;
        if (onToken)
            onToken(juce::String::fromUTF8("\n[compile failed, fixing...]\n"));
        userTurn = originalPrompt + "\n\n(Your previous attempt failed to compile:\n```\n" +
                   juce::String(result.source) + "\n```\nCompiler error:\n" +
                   juce::String(compileErr) + "\nFix it and output the corrected JSON object.)";
    }

    result = Result{};
    result.hasError = true;
    result.error = "Faust compilation still failing after " + std::to_string(kMaxAttempts) +
                   " attempts:\n" + lastError;
    logFaustAgentResult(result);
    return result;
}

}  // namespace magda
