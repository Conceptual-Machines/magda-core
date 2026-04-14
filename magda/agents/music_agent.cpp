#include "music_agent.hpp"

#include "../daw/core/Config.hpp"
#include "llm_client_factory.hpp"
#include "llm_presets.hpp"

namespace magda {

// ============================================================================
// System prompts
// ============================================================================

const char* MusicAgent::getCompactSystemPrompt() {
    return R"PROMPT(You are a music theory assistant. Generate musical content using compact notation.
Respond ONLY with instructions. No prose. No markdown. One instruction per line.

INSTRUCTIONS:
  CHORD <root> <quality> <beat> <length> [velocity]  - Add a chord
  NOTE <pitch> <beat> <length> [velocity]             - Add a single note
  ARP <root> <quality> <beat> <step> [beats]          - Add an arpeggio

ROOTS: C3, C#4, Db4, D4, Eb4, E4, F4, F#4, G4, Ab4, A4, Bb4, B4 (octave 3-5)
QUALITIES: major, min, dim, aug, sus2, sus4, dom7, maj7, min7, dim7, dom9, min9, maj9
BEAT: position in beats (0 = start, 4 = beat 5)
LENGTH: duration in beats
VELOCITY: 1-127 (optional, default 100)

EXAMPLES:
"C major chord progression" ->
CHORD C4 major 0 4
CHORD F4 major 4 4
CHORD G4 major 8 4
CHORD C4 major 12 4

"jazzy ii-V-I in Bb" ->
CHORD C4 min7 0 4
CHORD F4 dom7 4 4
CHORD Bb3 maj7 8 4

"blues in G" ->
CHORD G3 dom7 0 4
CHORD G3 dom7 4 4
CHORD C4 dom7 8 4
CHORD G3 dom7 12 4
CHORD D4 dom7 16 4
CHORD C4 dom7 20 4
CHORD G3 dom7 24 4
CHORD G3 dom7 28 4

"arpeggiate Am" ->
ARP A3 min 0 0.5

"bass line in E minor" ->
NOTE E2 0 1
NOTE G2 1 0.5
NOTE A2 1.5 0.5
NOTE B2 2 1
NOTE E2 3 1)PROMPT";
}

const char* MusicAgent::getDSLSystemPrompt() {
    return R"PROMPT(You are a music theory assistant. Generate musical content using DSL notation.
Your output must start with a DESCRIPTION line, then DSL note operations. No other prose.

FORMAT:
DESCRIPTION: <one sentence describing what you generated and why>
<DSL note operations, one per line>

NOTE OPERATIONS:
- notes.add(pitch=C4, beat=0, length=1, velocity=100) - Add a single note
- notes.add_chord(root=C4, quality=major, beat=0, length=1, velocity=100, inversion=0) - Add a chord
- notes.add_arpeggio(root=C4, quality=major, beat=0, step=0.5, beats=8, pattern=up) - Add an arpeggio

PITCH: C3, C#4, Db4, D4, Eb4, E4, F4, F#4, G4, Ab4, A4, Bb4, B4 (octave 2-6)
QUALITIES: major, min, dim, aug, sus2, sus4, dom7, maj7, min7, dim7, dom9, min9, maj9, 6, min6, 7b5, 7sharp5, half_dim, power
BEAT: position in beats (0 = start, 4 = beat 5)
LENGTH: duration in beats
VELOCITY: 1-127 (default 100)
INVERSION: 0=root, 1=first, 2=second (default 0)
PATTERN: up, down, updown (default up)

EXAMPLES:
"C major chord progression" ->
DESCRIPTION: Classic I-IV-V-I progression in C major, 4 beats per chord
notes.add_chord(root=C4, quality=major, beat=0, length=4)
notes.add_chord(root=F4, quality=major, beat=4, length=4)
notes.add_chord(root=G4, quality=major, beat=8, length=4)
notes.add_chord(root=C4, quality=major, beat=12, length=4)

"jazzy ii-V-I in Bb" ->
DESCRIPTION: Jazz ii-V-I turnaround in Bb major with seventh chords
notes.add_chord(root=C4, quality=min7, beat=0, length=4)
notes.add_chord(root=F4, quality=dom7, beat=4, length=4)
notes.add_chord(root=Bb3, quality=maj7, beat=8, length=4)

"arpeggiate Am" ->
DESCRIPTION: A minor arpeggio with eighth-note steps
notes.add_arpeggio(root=A3, quality=min, beat=0, step=0.5)

"bass line in E minor" ->
DESCRIPTION: Simple E minor bass line with a scalar walk-up from E2 to B2
notes.add(pitch=E2, beat=0, length=1)
notes.add(pitch=G2, beat=1, length=0.5)
notes.add(pitch=A2, beat=1.5, length=0.5)
notes.add(pitch=B2, beat=2, length=1)
notes.add(pitch=E2, beat=3, length=1)

CRITICAL: Always start with DESCRIPTION. Then only DSL operations. No other text.)PROMPT";
}

// ============================================================================
// DSL parser — converts DSL note operations into IR instructions
// ============================================================================

namespace {

/** Extract key=value pairs from a parameter string like "root=C4, quality=major, beat=0". */
juce::StringPairArray parseParams(const juce::String& paramStr) {
    juce::StringPairArray params;
    auto pairs = juce::StringArray::fromTokens(paramStr, ",", "\"");
    for (auto& pair : pairs) {
        auto eqPos = pair.indexOf("=");
        if (eqPos > 0) {
            auto key = pair.substring(0, eqPos).trim();
            auto val = pair.substring(eqPos + 1).trim();
            params.set(key, val);
        }
    }
    return params;
}

}  // namespace

std::vector<Instruction> MusicAgent::parseDSL(const juce::String& text,
                                              std::string& outDescription) {
    std::vector<Instruction> instructions;
    auto lines = juce::StringArray::fromLines(text);

    for (auto& line : lines) {
        auto trimmed = line.trim();
        if (trimmed.isEmpty())
            continue;

        // Extract description
        if (trimmed.startsWith("DESCRIPTION:")) {
            outDescription = trimmed.substring(12).trim().toStdString();
            continue;
        }

        // Skip comments
        if (trimmed.startsWith("//"))
            continue;

        // Parse notes.add_chord(...)
        if (trimmed.startsWith("notes.add_chord(") && trimmed.endsWith(")")) {
            auto paramStr = trimmed.substring(16, trimmed.length() - 1);
            auto params = parseParams(paramStr);

            ChordOp op;
            op.root = params.getValue("root", "C4");
            op.quality = params.getValue("quality", "major");
            op.beat = params.getValue("beat", "0").getDoubleValue();
            op.length = params.getValue("length", "1").getDoubleValue();
            auto vel = params.getValue("velocity", "");
            if (vel.isNotEmpty())
                op.velocity = vel.getIntValue();
            auto inv = params.getValue("inversion", "");
            if (inv.isNotEmpty())
                op.inversion = inv.getIntValue();

            instructions.push_back({OpCode::Chord, std::move(op)});
            continue;
        }

        // Parse notes.add_arpeggio(...)
        if (trimmed.startsWith("notes.add_arpeggio(") && trimmed.endsWith(")")) {
            auto paramStr = trimmed.substring(19, trimmed.length() - 1);
            auto params = parseParams(paramStr);

            ArpOp op;
            op.root = params.getValue("root", "C4");
            op.quality = params.getValue("quality", "major");
            op.beat = params.getValue("beat", "0").getDoubleValue();
            op.step = params.getValue("step", "0.5").getDoubleValue();
            auto beats = params.getValue("beats", "");
            if (beats.isNotEmpty())
                op.beats = beats.getDoubleValue();
            auto inv = params.getValue("inversion", "");
            if (inv.isNotEmpty())
                op.inversion = inv.getIntValue();
            op.pattern = params.getValue("pattern", "");

            instructions.push_back({OpCode::Arp, std::move(op)});
            continue;
        }

        // Parse notes.add(...)
        if (trimmed.startsWith("notes.add(") && trimmed.endsWith(")")) {
            auto paramStr = trimmed.substring(10, trimmed.length() - 1);
            auto params = parseParams(paramStr);

            NoteOp op;
            op.pitch = params.getValue("pitch", "C4");
            op.beat = params.getValue("beat", "0").getDoubleValue();
            op.length = params.getValue("length", "1").getDoubleValue();
            auto vel = params.getValue("velocity", "");
            if (vel.isNotEmpty())
                op.velocity = vel.getIntValue();

            instructions.push_back({OpCode::Note, std::move(op)});
            continue;
        }
    }

    return instructions;
}

namespace {

/** Log the resolved LLM config before a request — makes provider/endpoint/model
    mismatches obvious in the console. Does NOT log the API key. */
void logMusicAgentConfig(const Config::AgentLLMConfig& agentConfig, const llm::ProviderConfig& pc,
                         bool useCompact) {
    DBG("MAGDA MusicAgent config:");
    DBG("  provider (string) = " + juce::String(agentConfig.provider));
    DBG("  provider (enum)   = " + juce::String(static_cast<int>(pc.provider)) +
        " (0=OpenAIChat, 1=OpenAIResponses, 2=Anthropic, 3=Gemini)");
    DBG("  model             = " + pc.model);
    DBG("  baseUrl           = " + pc.baseUrl);
    DBG("  apiKey present    = " + juce::String(pc.apiKey.isNotEmpty() ? "yes" : "NO"));
    DBG("  noTemperature     = " + juce::String(pc.noTemperature ? "yes" : "no"));
    DBG("  reasoningEffort   = " + pc.reasoningEffort);
    DBG("  format            = " + juce::String(useCompact ? "compact" : "DSL"));
}

void logMusicAgentResult(const std::string& rawOutput, const std::vector<Instruction>& instructions,
                         const std::string& description, const juce::String& error) {
    DBG("MAGDA MusicAgent raw output (" + juce::String(static_cast<int>(rawOutput.size())) +
        " chars):");
    DBG("---8<---");
    DBG(juce::String(rawOutput));
    DBG("--->8---");
    DBG("MAGDA MusicAgent parsed " + juce::String(static_cast<int>(instructions.size())) +
        " instruction(s)");
    if (!description.empty())
        DBG("MAGDA MusicAgent description: " + juce::String(description));
    if (error.isNotEmpty())
        DBG("MAGDA MusicAgent ERROR: " + error);
}

}  // namespace

MusicAgent::GenerateResult MusicAgent::generate(const std::string& message) {
    GenerateResult result;

    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    auto agentConfig = Config::getInstance().getAgentLLMConfig(role::MUSIC);
    bool useCompact = (agentConfig.provider == provider::LLAMA_LOCAL);

    auto providerConfig = toLLMProviderConfig(agentConfig, "music");
    logMusicAgentConfig(agentConfig, providerConfig, useCompact);

    if (!useCompact) {
        if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty()) {
            result.error = "Music agent API key not configured.";
            result.hasError = true;
            DBG("MAGDA MusicAgent ABORT: no API key for provider " +
                juce::String(agentConfig.provider));
            return result;
        }
    }

    auto client = createLLMClient(agentConfig, "music");
    DBG("MAGDA MusicAgent client name: " + client->getName());

    llm::Request request;
    request.systemPrompt =
        juce::String::fromUTF8(useCompact ? getCompactSystemPrompt() : getDSLSystemPrompt());
    request.userMessage = juce::String::fromUTF8(message.c_str());
    request.temperature = 0.3f;  // slightly more creative for music

    DBG("MAGDA MusicAgent sending request (user message: " + request.userMessage + ")");

    auto response = client->sendRequest(request);

    DBG("MAGDA MusicAgent response: success=" + juce::String(response.success ? "true" : "false") +
        " wall=" + juce::String(response.wallSeconds, 2) + "s");

    if (!response.success) {
        DBG("MAGDA MusicAgent HTTP/provider error: " + response.error);
        result.error = response.error.toStdString();
        result.hasError = true;
        return result;
    }

    auto trimmedText = response.text.trim();
    result.rawOutput = trimmedText.toStdString();

    if (useCompact) {
        result.instructions = parser_.parse(trimmedText);
        if (result.instructions.empty() && parser_.getLastError().isNotEmpty()) {
            result.error = "Parse error: " + parser_.getLastError().toStdString();
            result.hasError = true;
        }
    } else {
        result.instructions = parseDSL(trimmedText, result.description);
        if (result.instructions.empty()) {
            result.error = "DSL parse error: no valid note operations found";
            result.hasError = true;
        }
    }

    logMusicAgentResult(result.rawOutput, result.instructions, result.description,
                        juce::String(result.error));

    return result;
}

MusicAgent::GenerateResult MusicAgent::generateStreaming(const std::string& message,
                                                         TokenCallback onToken) {
    GenerateResult result;

    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    auto agentConfig = Config::getInstance().getAgentLLMConfig(role::MUSIC);
    bool useCompact = (agentConfig.provider == provider::LLAMA_LOCAL);

    auto providerConfig = toLLMProviderConfig(agentConfig, "music");
    logMusicAgentConfig(agentConfig, providerConfig, useCompact);

    if (!useCompact) {
        if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty()) {
            result.error = "Music agent API key not configured.";
            result.hasError = true;
            DBG("MAGDA MusicAgent stream ABORT: no API key for provider " +
                juce::String(agentConfig.provider));
            return result;
        }
    }

    auto client = createLLMClient(agentConfig, "music");
    DBG("MAGDA MusicAgent stream client name: " + client->getName());

    llm::Request request;
    request.systemPrompt =
        juce::String::fromUTF8(useCompact ? getCompactSystemPrompt() : getDSLSystemPrompt());
    request.userMessage = juce::String::fromUTF8(message.c_str());
    request.temperature = 0.3f;

    DBG("MAGDA MusicAgent stream sending request (user message: " + request.userMessage + ")");

    auto response = client->sendStreamingRequest(request, [&](const juce::String& token) {
        if (shouldStop_.load())
            return false;
        if (onToken)
            return onToken(token);
        return true;
    });

    DBG("MAGDA MusicAgent stream response: success=" +
        juce::String(response.success ? "true" : "false") +
        " wall=" + juce::String(response.wallSeconds, 2) + "s");

    if (!response.success) {
        DBG("MAGDA MusicAgent stream HTTP/provider error: " + response.error);
        result.error = response.error.toStdString();
        result.hasError = true;
        return result;
    }

    auto trimmedText = response.text.trim();
    result.rawOutput = trimmedText.toStdString();

    if (useCompact) {
        result.instructions = parser_.parse(trimmedText);
        if (result.instructions.empty() && parser_.getLastError().isNotEmpty()) {
            result.error = "Parse error: " + parser_.getLastError().toStdString();
            result.hasError = true;
        }
    } else {
        result.instructions = parseDSL(trimmedText, result.description);
        if (result.instructions.empty()) {
            result.error = "DSL parse error: no valid note operations found";
            result.hasError = true;
        }
    }

    logMusicAgentResult(result.rawOutput, result.instructions, result.description,
                        juce::String(result.error));

    return result;
}

}  // namespace magda
