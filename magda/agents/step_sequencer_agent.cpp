#include "step_sequencer_agent.hpp"

#include "../daw/core/Config.hpp"
#include "llm_client_factory.hpp"
#include "llm_presets.hpp"

namespace magda {

// ============================================================================
// System prompt
// ============================================================================
//
// StepClock::Rate enum values (for the `rate` field):
//   0 = DottedQuarter  (dotted 1/4)
//   1 = Quarter        (1/4)
//   2 = TripletQuarter (1/4T)
//   3 = DottedEighth   (dotted 1/8)
//   4 = Eighth         (1/8)
//   5 = TripletEighth  (1/8T)
//   6 = DottedSixteenth (dotted 1/16)
//   7 = Sixteenth      (1/16)    <-- most common default
//   8 = TripletSixteenth (1/16T)
//   9 = ThirtySecond   (1/32)

const char* StepSequencerAgent::getSystemPrompt() {
    return R"PROMPT(You are a pattern designer for the MAGDA built-in mono Step Sequencer (303-style).
Given a user description, produce a single step-sequencer pattern as JSON. Output ONLY the JSON
object -- no prose, no markdown fences.

The Step Sequencer is monophonic: each step plays one note at a time, with accent, glide (slide),
and tie controls. It is designed for acid basslines, melodic sequences, and arpeggios. Steps fire
on tempo-synced beats.

OUTPUT SCHEMA:
{
  "description": "<one short line describing the pattern>",
  "numSteps": <1-32, integer, OPTIONAL -- omit to keep the current value>,
  "rate": <0-9, integer, OPTIONAL -- omit to keep the current value>,
  "swing": <0.0-1.0, float, OPTIONAL -- omit to keep the current value>,
  "gateLength": <0.0-1.0, float, OPTIONAL -- omit to keep the current value>,
  "steps": [
    {
      "index": <0-based integer>,
      "note": <0-127 MIDI note number>,
      "octave": <-2 to +2, integer, applied on top of note>,
      "gate": <true|false>,
      "accent": <true|false, optional, default false>,
      "glide": <true|false, optional, default false>,
      "tie": <true|false, optional, default false>
    }
  ]
}

RATE TABLE (use the integer, not the string):
  0  = dotted 1/4  (slow, half-bar feel)
  1  = 1/4         (quarter notes)
  2  = 1/4T        (quarter note triplets)
  3  = dotted 1/8
  4  = 1/8         (eighth notes)
  5  = 1/8T        (eighth triplets)
  6  = dotted 1/16
  7  = 1/16        (sixteenth notes -- DEFAULT for most patterns)
  8  = 1/16T       (sixteenth triplets)
  9  = 1/32        (thirty-second notes, very fast)

STEPS RULES:
- Emit ALL numSteps steps. Every step in 0..(numSteps-1) must appear in the array.
- `gate: true` means the step plays a note. `gate: false` is a rest -- include it.
- `note` is the base MIDI note number (0-127). C4 = 60, D4 = 62, E4 = 64, F4 = 65,
  G4 = 67, A4 = 69, B4 = 71, C3 = 48, C2 = 36.
- `octave` is an additional shift of -2 to +2 octaves (12 semitones per octave).
  Keep octave at 0 unless the user asks for wide range. The effective MIDI note
  is note + octave * 12 (clamped to 0-127).
- `accent: true` flags a step as accented (louder velocity). Use for rhythmic emphasis.
- `glide: true` applies portamento: the previous note slides into this step's pitch.
  Classic in acid basslines. Glide and tie are mutually exclusive.
- `tie: true` extends the previous step's note without retriggering. Use for sustained
  notes across adjacent steps. Tie and glide are mutually exclusive.
- For rest steps, set gate=false and any convenient note value (it won't sound).

numSteps GUIDELINES:
- 16 steps at 1/16 rate = one bar (most common)
- 8 steps at 1/16 rate = half bar
- 32 steps at 1/16 rate = two bars

swing: 0.0 = straight. 0.5 = heavy swing. 0.1-0.2 = light shuffle.
gateLength: 0.1 = very staccato. 0.5 = moderate. 0.9 = near-legato.
  For acid basslines use 0.3-0.5; add glide separately where needed.

DEVICE CONTEXT (may be appended after this prompt):
When a "DEVICE CONTEXT:" block is appended, it shows the current device settings.

PRESERVING CURRENT SETTINGS:
- The "currentSettings:" line shows the live values for numSteps, rate, swing,
  and gateLength.
- PRESERVE all of these unless the user's prompt explicitly implies changing
  them (e.g. "make it 8 steps", "use eighth notes", "add swing", "double time").
- To preserve a value, simply OMIT that field from your JSON output. The apply
  code will leave the device value unchanged when the field is absent.
- CRITICAL: always write a pattern that spans the full current numSteps. If
  currentSettings shows numSteps=32, emit all 32 steps (indices 0-31). The mono
  sequencer requires ALL steps to be listed (no sparse output).

ACID BASSLINE GUIDELINES:
- Root, fifth, minor seventh, minor third are the classic acid intervals.
  E.g. in A minor: A2(45) A3(57) E3(52) G2(43) C3(48).
- Accent beats 1 and 3 (indices 0, 8 in 16-step). Accent random off-beats for energy.
- Glide on every 2nd or 3rd step creates the characteristic 303 slide.
- Mix stepwise movement with jumps of a fourth or fifth.
- Use 1/16 rate (7) and gateLength 0.3-0.5 for the classic staccato feel.

MELODY GUIDELINES:
- Keep notes within a scale (major, minor, pentatonic).
- Use rests (gate=false) for rhythmic breathing.
- Tie adjacent steps for legato phrases.
- Octave jumps (octave +/-1) add excitement.

EXAMPLES:

User: "classic acid bassline in A minor, 16 steps"
{"description":"Acid bassline in A minor, 16 steps 1/16","numSteps":16,"rate":7,"swing":0.0,"gateLength":0.4,"steps":[{"index":0,"note":45,"octave":0,"gate":true,"accent":true,"glide":false,"tie":false},{"index":1,"note":45,"octave":0,"gate":true,"accent":false,"glide":true,"tie":false},{"index":2,"note":48,"octave":0,"gate":true,"accent":false,"glide":false,"tie":false},{"index":3,"note":45,"octave":0,"gate":false,"accent":false,"glide":false,"tie":false},{"index":4,"note":52,"octave":0,"gate":true,"accent":true,"glide":false,"tie":false},{"index":5,"note":52,"octave":0,"gate":true,"accent":false,"glide":true,"tie":false},{"index":6,"note":50,"octave":0,"gate":true,"accent":false,"glide":false,"tie":false},{"index":7,"note":48,"octave":0,"gate":true,"accent":false,"glide":false,"tie":false},{"index":8,"note":45,"octave":0,"gate":true,"accent":true,"glide":false,"tie":false},{"index":9,"note":43,"octave":0,"gate":true,"accent":false,"glide":true,"tie":false},{"index":10,"note":45,"octave":0,"gate":true,"accent":false,"glide":false,"tie":false},{"index":11,"note":45,"octave":0,"gate":false,"accent":false,"glide":false,"tie":false},{"index":12,"note":48,"octave":0,"gate":true,"accent":false,"glide":false,"tie":false},{"index":13,"note":50,"octave":0,"gate":true,"accent":false,"glide":true,"tie":false},{"index":14,"note":52,"octave":0,"gate":true,"accent":true,"glide":false,"tie":false},{"index":15,"note":45,"octave":0,"gate":true,"accent":false,"glide":false,"tie":false}]}

User: "simple 8-step C major melody"
{"description":"Simple C major melody, 8 steps 1/8","numSteps":8,"rate":4,"swing":0.0,"gateLength":0.6,"steps":[{"index":0,"note":60,"octave":0,"gate":true,"accent":true,"glide":false,"tie":false},{"index":1,"note":62,"octave":0,"gate":true,"accent":false,"glide":false,"tie":false},{"index":2,"note":64,"octave":0,"gate":true,"accent":false,"glide":false,"tie":false},{"index":3,"note":65,"octave":0,"gate":false,"accent":false,"glide":false,"tie":false},{"index":4,"note":67,"octave":0,"gate":true,"accent":true,"glide":false,"tie":false},{"index":5,"note":67,"octave":0,"gate":true,"accent":false,"glide":false,"tie":true},{"index":6,"note":64,"octave":0,"gate":true,"accent":false,"glide":false,"tie":false},{"index":7,"note":60,"octave":0,"gate":true,"accent":false,"glide":false,"tie":false}]}
)PROMPT";
}

// ============================================================================
// JSON parser
// ============================================================================

StepSequencerAgent::Preset StepSequencerAgent::parseJson(const juce::String& text,
                                                         std::string& outError) {
    Preset preset;

    // Strip LLM markdown fences if present.
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
        outError = "preset JSON must be an object";
        return preset;
    }

    if (auto desc = obj->getProperty("description"); desc.isString())
        preset.description = desc.toString().toStdString();

    if (auto ns = obj->getProperty("numSteps"); ns.isInt() || ns.isDouble())
        preset.numSteps = juce::jlimit(1, 32, static_cast<int>(static_cast<double>(ns)));

    if (auto r = obj->getProperty("rate"); r.isInt() || r.isDouble())
        preset.rate = juce::jlimit(0, 9, static_cast<int>(static_cast<double>(r)));

    if (auto sw = obj->getProperty("swing"); sw.isDouble() || sw.isInt())
        preset.swing = juce::jlimit(0.0f, 1.0f, static_cast<float>(static_cast<double>(sw)));

    if (auto gl = obj->getProperty("gateLength"); gl.isDouble() || gl.isInt())
        preset.gateLength = juce::jlimit(0.0f, 1.0f, static_cast<float>(static_cast<double>(gl)));

    // Parse steps array.
    auto stepsVar = obj->getProperty("steps");
    if (!stepsVar.isArray()) {
        outError = "preset JSON missing 'steps' array";
        return preset;
    }

    auto* stepsArr = stepsVar.getArray();
    if (stepsArr == nullptr) {
        outError = "preset JSON 'steps' is null";
        return preset;
    }

    for (const auto& stepVar : *stepsArr) {
        auto* stepObj = stepVar.getDynamicObject();
        if (stepObj == nullptr)
            continue;

        StepSpec step;

        auto idxVar = stepObj->getProperty("index");
        if (!idxVar.isInt() && !idxVar.isDouble())
            continue;  // index is required
        step.index = juce::jlimit(0, 31, static_cast<int>(static_cast<double>(idxVar)));

        if (auto n = stepObj->getProperty("note"); n.isInt() || n.isDouble())
            step.noteNumber = juce::jlimit(0, 127, static_cast<int>(static_cast<double>(n)));

        if (auto o = stepObj->getProperty("octave"); o.isInt() || o.isDouble())
            step.octaveShift = juce::jlimit(-2, 2, static_cast<int>(static_cast<double>(o)));

        if (auto g = stepObj->getProperty("gate"); g.isBool() || g.isInt())
            step.gate = static_cast<bool>(g);
        else
            step.gate = true;

        if (auto a = stepObj->getProperty("accent"); a.isBool() || a.isInt())
            step.accent = static_cast<bool>(a);

        if (auto gl = stepObj->getProperty("glide"); gl.isBool() || gl.isInt())
            step.glide = static_cast<bool>(gl);

        if (auto t = stepObj->getProperty("tie"); t.isBool() || t.isInt())
            step.tie = static_cast<bool>(t);

        preset.steps.push_back(std::move(step));
    }

    return preset;
}

// ============================================================================
// LLM call
// ============================================================================

namespace {

void logStepAgentConfig(const Config::AgentLLMConfig& agentConfig, const llm::ProviderConfig& pc) {
    DBG("MAGDA StepSequencerAgent config:");
    DBG("  provider (string) = " + juce::String(agentConfig.provider));
    DBG("  provider (enum)   = " + juce::String(static_cast<int>(pc.provider)));
    DBG("  model             = " + pc.model);
    DBG("  baseUrl           = " + pc.baseUrl);
    DBG("  apiKey present    = " + juce::String(pc.apiKey.isNotEmpty() ? "yes" : "NO"));
    DBG("  noTemperature     = " + juce::String(pc.noTemperature ? "yes" : "no"));
}

void logStepAgentResult(const StepSequencerAgent::GenerateResult& result) {
    DBG("MAGDA StepSequencerAgent raw output (" +
        juce::String(static_cast<int>(result.rawOutput.size())) + " chars):");
    DBG("---8<---");
    DBG(juce::String(result.rawOutput));
    DBG("--->8---");
    DBG("MAGDA StepSequencerAgent parsed: description='" + juce::String(result.preset.description) +
        "' steps=" + juce::String(static_cast<int>(result.preset.steps.size())));
    if (!result.error.empty())
        DBG("MAGDA StepSequencerAgent ERROR: " + juce::String(result.error));
}

}  // namespace

StepSequencerAgent::GenerateResult StepSequencerAgent::generate(const std::string& message,
                                                                const std::string& deviceContext) {
    GenerateResult result;

    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    auto agentConfig = Config::getInstance().getAgentLLMConfig(role::MUSIC);
    auto providerConfig = toLLMProviderConfig(agentConfig, "step_sequencer");
    logStepAgentConfig(agentConfig, providerConfig);

    if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty() &&
        agentConfig.provider != provider::LLAMA_LOCAL) {
        result.error = "Step Sequencer agent API key not configured.";
        result.hasError = true;
        return result;
    }

    auto client = createLLMClient(agentConfig, "step_sequencer");

    llm::Request request;
    auto systemPrompt = juce::String::fromUTF8(getSystemPrompt());
    if (!deviceContext.empty())
        systemPrompt += "\n\n" + juce::String::fromUTF8(deviceContext.c_str());
    request.systemPrompt = systemPrompt;
    request.userMessage = juce::String::fromUTF8(message.c_str());
    request.temperature = 0.5f;  // some creativity for patterns

    auto response = client->sendRequest(request);
    if (!response.success) {
        result.error = response.error.toStdString();
        result.hasError = true;
        return result;
    }

    auto trimmedText = response.text.trim();
    result.rawOutput = trimmedText.toStdString();

    std::string parseError;
    result.preset = parseJson(trimmedText, parseError);
    if (!parseError.empty()) {
        result.error = parseError;
        result.hasError = true;
    }

    logStepAgentResult(result);
    return result;
}

StepSequencerAgent::GenerateResult StepSequencerAgent::generateStreaming(
    const std::string& message, TokenCallback onToken, const std::string& deviceContext) {
    GenerateResult result;

    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    auto agentConfig = Config::getInstance().getAgentLLMConfig(role::MUSIC);
    auto providerConfig = toLLMProviderConfig(agentConfig, "step_sequencer");
    logStepAgentConfig(agentConfig, providerConfig);

    if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty() &&
        agentConfig.provider != provider::LLAMA_LOCAL) {
        result.error = "Step Sequencer agent API key not configured.";
        result.hasError = true;
        return result;
    }

    auto client = createLLMClient(agentConfig, "step_sequencer");

    llm::Request request;
    auto systemPromptStr = juce::String::fromUTF8(getSystemPrompt());
    if (!deviceContext.empty())
        systemPromptStr += "\n\n" + juce::String::fromUTF8(deviceContext.c_str());
    request.systemPrompt = systemPromptStr;
    request.userMessage = juce::String::fromUTF8(message.c_str());
    request.temperature = 0.5f;

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

    auto trimmedText = response.text.trim();
    result.rawOutput = trimmedText.toStdString();

    std::string parseError;
    result.preset = parseJson(trimmedText, parseError);
    if (!parseError.empty()) {
        result.error = parseError;
        result.hasError = true;
    }

    logStepAgentResult(result);
    return result;
}

}  // namespace magda
