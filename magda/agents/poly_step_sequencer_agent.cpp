#include "poly_step_sequencer_agent.hpp"

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

const char* PolyStepSequencerAgent::getSystemPrompt() {
    return R"PROMPT(You are a pattern designer for the MAGDA built-in Poly Step Sequencer. Given a user
description, produce a single step-sequencer pattern as JSON. Output ONLY the JSON object --
no prose, no markdown fences.

The Poly Step Sequencer fires MIDI note events on tempo-synced steps. Each step can hold
up to 8 simultaneous notes (a chord), plus per-step gate / tie / probability / velocity
controls. It works for both melodic/chordal patterns and drum patterns.

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
      "gate": <true|false>,
      "tie": <true|false, optional, default false>,
      "probability": <0.0-1.0, float, optional, default 1.0>,
      "velocity": <1-127, integer, optional, default 100>,
      "notes": [
        { "note": <0-127 MIDI note number> },
        { "note": <0-127>, "velocity": <1-127> }
      ]
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
- Only emit steps that have notes. Steps not listed are cleared (gate=off).
- `gate: true` means the step fires. `gate: false` means rest -- include it
  only when you want an explicit rest in an otherwise active range.
- `tie: true` extends the previous step's notes without retrigger (like a
  note tie/slur). Use for longer sustained notes across adjacent steps.
- `probability` less than 1.0 injects randomness. 0.75 = fires 3 out of 4
  times on average. Good for hi-hat variation.
- `velocity` is the step-level MIDI velocity (1-127). Per-note `velocity`
  overrides the step value for that specific note only; omit it to inherit.
- `notes` may be empty when `gate: false` (rest step). For active steps
  always include at least one note.
- Note numbers follow standard MIDI: C4 = 60, D4 = 62, E4 = 64, F4 = 65,
  G4 = 67, A4 = 69, B4 = 71, C5 = 72.

DRUM MAP (General MIDI, use for drum patterns):
  36 = Kick (Bass Drum)
  38 = Snare
  39 = Hand Clap
  40 = Snare (electric)
  42 = Hi-hat closed
  44 = Hi-hat pedal
  46 = Hi-hat open
  49 = Crash cymbal
  51 = Ride cymbal
  52 = China cymbal
  57 = Crash 2

MELODY / CHORD VOICINGS:
- For single-note basslines: one note per step, root of the scale.
- For power chords: root + 7 semitones (e.g. 48 + 55 for C2 power chord).
- For triads (major): root, root+4, root+7. Minor: root, root+3, root+7.
- For pads / sustained chords: set gateLength close to 1.0 and use tie on
  tied steps.
- Typical octave ranges: bass 24-48, mid 48-72, high 72-96.

numSteps GUIDELINES:
- 16 steps: standard for 1-bar patterns at 1/16 rate
- 8 steps: half-bar, good for fast hi-hat rolls
- 32 steps: 2-bar phrase
- For drum patterns 16 is almost always correct.

swing: 0.0 = straight. 0.5 = heavy swing. 0.1-0.2 = light shuffle.
gateLength: 0.1 = very staccato. 0.5 = moderate. 0.9 = near-legato.
  Default 0.5 works for most patterns; use 0.2-0.3 for drums/plucks.

DEVICE CONTEXT (may be appended after this prompt):
When a "DEVICE CONTEXT:" block is appended, it describes the current state of
the device the user is editing. Follow these rules:

PRESERVING CURRENT SETTINGS:
- The "currentSettings:" line shows the live device values for numSteps, rate,
  swing, and gateLength.
- PRESERVE all of these unless the user's prompt explicitly implies changing
  them (e.g. "make it 8 steps", "use eighth notes", "add swing", "double time").
- To preserve a value, simply OMIT that field from your JSON output. The apply
  code will leave the device value unchanged when the field is absent.
- CRITICAL: always write a pattern that spans the full current numSteps. If
  currentSettings shows numSteps=32, produce step indices 0-31 as needed
  (sparse is fine -- only emit active steps). Do NOT silently shrink the
  pattern to 16 steps.

- viewMode=keys: the user is editing melodic or chordal material on a piano-roll
  view. Produce chords, melodies, arpeggios, or basslines. Do NOT produce drum
  patterns (kick/snare/hi-hat using GM note numbers) unless the prompt
  explicitly asks for drums.

- viewMode=drum: the user is editing a drum pattern on a drum-lanes view.
  Produce drum patterns. If a LANE MAP is provided, use exactly those note
  numbers and reference the pad names in the description. Without a lane map,
  fall back to the GM DRUM MAP above.

EXAMPLES:

User: "basic 4-on-the-floor kick pattern"
{"description":"4-on-the-floor kick, 16 steps at 1/16","numSteps":16,"rate":7,"swing":0.0,"gateLength":0.25,"steps":[{"index":0,"gate":true,"velocity":110,"notes":[{"note":36}]},{"index":4,"gate":true,"velocity":110,"notes":[{"note":36}]},{"index":8,"gate":true,"velocity":110,"notes":[{"note":36}]},{"index":12,"gate":true,"velocity":110,"notes":[{"note":36}]}]}

User: "classic trap hi-hat pattern with rolls"
{"description":"Trap hi-hat: 1/32 pattern with rolls and velocity variation","numSteps":16,"rate":9,"swing":0.1,"gateLength":0.15,"steps":[{"index":0,"gate":true,"velocity":100,"notes":[{"note":42}]},{"index":2,"gate":true,"velocity":80,"notes":[{"note":42}]},{"index":4,"gate":true,"velocity":100,"notes":[{"note":42}]},{"index":5,"gate":true,"velocity":70,"notes":[{"note":42}]},{"index":6,"gate":true,"velocity":90,"notes":[{"note":42}]},{"index":8,"gate":true,"velocity":100,"notes":[{"note":46}]},{"index":10,"gate":true,"velocity":80,"notes":[{"note":42}]},{"index":12,"gate":true,"velocity":100,"notes":[{"note":42}]},{"index":14,"gate":true,"velocity":80,"notes":[{"note":42}]},{"index":15,"gate":true,"velocity":70,"notes":[{"note":42}]}]}

User: "chord progression Cmaj - Am - F - G, one chord per 4 steps"
{"description":"Cmaj-Am-F-G chord progression, four chords over 16 steps","numSteps":16,"rate":7,"swing":0.0,"gateLength":0.9,"steps":[{"index":0,"gate":true,"velocity":90,"notes":[{"note":60},{"note":64},{"note":67}]},{"index":4,"gate":true,"velocity":90,"notes":[{"note":57},{"note":60},{"note":64}]},{"index":8,"gate":true,"velocity":90,"notes":[{"note":53},{"note":57},{"note":60}]},{"index":12,"gate":true,"velocity":90,"notes":[{"note":55},{"note":59},{"note":62}]}]}

User: "driving 16th note bass line in A minor"
{"description":"Driving 16th-note bass in A minor","numSteps":16,"rate":7,"swing":0.0,"gateLength":0.4,"steps":[{"index":0,"gate":true,"velocity":105,"notes":[{"note":45}]},{"index":1,"gate":true,"velocity":85,"notes":[{"note":45}]},{"index":2,"gate":true,"velocity":90,"notes":[{"note":48}]},{"index":3,"gate":true,"velocity":80,"notes":[{"note":50}]},{"index":4,"gate":true,"velocity":100,"notes":[{"note":52}]},{"index":5,"gate":true,"velocity":80,"notes":[{"note":50}]},{"index":6,"gate":true,"velocity":85,"notes":[{"note":48}]},{"index":7,"gate":true,"velocity":75,"notes":[{"note":45}]},{"index":8,"gate":true,"velocity":105,"notes":[{"note":45}]},{"index":9,"gate":true,"velocity":80,"notes":[{"note":45}]},{"index":10,"gate":true,"velocity":88,"notes":[{"note":48}]},{"index":11,"gate":true,"velocity":78,"notes":[{"note":52}]},{"index":12,"gate":true,"velocity":100,"notes":[{"note":55}]},{"index":13,"gate":true,"velocity":82,"notes":[{"note":53}]},{"index":14,"gate":true,"velocity":87,"notes":[{"note":52}]},{"index":15,"gate":true,"velocity":78,"notes":[{"note":48}]}]}
)PROMPT";
}

// ============================================================================
// JSON parser
// ============================================================================

PolyStepSequencerAgent::Preset PolyStepSequencerAgent::parseJson(const juce::String& text,
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

    if (auto ns = obj->getProperty("numSteps"); ns.isInt() || ns.isDouble()) {
        preset.numSteps = juce::jlimit(1, 32, static_cast<int>(static_cast<double>(ns)));
    }

    if (auto r = obj->getProperty("rate"); r.isInt() || r.isDouble()) {
        preset.rate = juce::jlimit(0, 9, static_cast<int>(static_cast<double>(r)));
    }

    if (auto sw = obj->getProperty("swing"); sw.isDouble() || sw.isInt()) {
        preset.swing = juce::jlimit(0.0f, 1.0f, static_cast<float>(static_cast<double>(sw)));
    }

    if (auto gl = obj->getProperty("gateLength"); gl.isDouble() || gl.isInt()) {
        preset.gateLength = juce::jlimit(0.0f, 1.0f, static_cast<float>(static_cast<double>(gl)));
    }

    // Parse steps array.
    auto stepsVar = obj->getProperty("steps");
    if (!stepsVar.isArray()) {
        outError = "preset JSON missing 'steps' array";
        return preset;
    }

    auto* stepsArr = stepsVar.getArray();
    if (stepsArr == nullptr || stepsArr->isEmpty()) {
        // An empty steps array is valid (clears all steps).
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

        if (auto g = stepObj->getProperty("gate"); g.isBool() || g.isInt())
            step.gate = static_cast<bool>(g);
        else
            step.gate = true;

        if (auto t = stepObj->getProperty("tie"); t.isBool() || t.isInt())
            step.tie = static_cast<bool>(t);

        if (auto p = stepObj->getProperty("probability"); p.isDouble() || p.isInt())
            step.probability = juce::jlimit(0.0f, 1.0f, static_cast<float>(static_cast<double>(p)));

        if (auto v = stepObj->getProperty("velocity"); v.isInt() || v.isDouble())
            step.velocity = juce::jlimit(1, 127, static_cast<int>(static_cast<double>(v)));

        // Parse notes array for this step.
        if (auto notesVar = stepObj->getProperty("notes"); notesVar.isArray()) {
            if (auto* notesArr = notesVar.getArray()) {
                for (const auto& noteVar : *notesArr) {
                    auto* noteObj = noteVar.getDynamicObject();
                    if (noteObj == nullptr)
                        continue;

                    NoteSpec note;
                    auto nv = noteObj->getProperty("note");
                    if (!nv.isInt() && !nv.isDouble())
                        continue;
                    note.noteNumber =
                        juce::jlimit(0, 127, static_cast<int>(static_cast<double>(nv)));

                    if (auto vel = noteObj->getProperty("velocity"); vel.isInt() || vel.isDouble())
                        note.velocityOverride =
                            juce::jlimit(1, 127, static_cast<int>(static_cast<double>(vel)));
                    // else velocityOverride stays 0 (= inherit step velocity)

                    step.notes.push_back(note);
                    if (static_cast<int>(step.notes.size()) >= 8)
                        break;  // MAX_NOTES_PER_STEP
                }
            }
        }

        preset.steps.push_back(std::move(step));
    }

    return preset;
}

// ============================================================================
// LLM call
// ============================================================================

namespace {

void logPolyStepAgentConfig(const Config::AgentLLMConfig& agentConfig,
                            const llm::ProviderConfig& pc) {
    DBG("MAGDA PolyStepSequencerAgent config:");
    DBG("  provider (string) = " + juce::String(agentConfig.provider));
    DBG("  provider (enum)   = " + juce::String(static_cast<int>(pc.provider)));
    DBG("  model             = " + pc.model);
    DBG("  baseUrl           = " + pc.baseUrl);
    DBG("  apiKey present    = " + juce::String(pc.apiKey.isNotEmpty() ? "yes" : "NO"));
    DBG("  noTemperature     = " + juce::String(pc.noTemperature ? "yes" : "no"));
}

void logPolyStepAgentResult(const PolyStepSequencerAgent::GenerateResult& result) {
    DBG("MAGDA PolyStepSequencerAgent raw output (" +
        juce::String(static_cast<int>(result.rawOutput.size())) + " chars):");
    DBG("---8<---");
    DBG(juce::String(result.rawOutput));
    DBG("--->8---");
    DBG("MAGDA PolyStepSequencerAgent parsed: description='" +
        juce::String(result.preset.description) +
        "' steps=" + juce::String(static_cast<int>(result.preset.steps.size())));
    if (!result.error.empty())
        DBG("MAGDA PolyStepSequencerAgent ERROR: " + juce::String(result.error));
}

}  // namespace

PolyStepSequencerAgent::GenerateResult PolyStepSequencerAgent::generate(
    const std::string& message, const std::string& deviceContext) {
    GenerateResult result;

    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    auto agentConfig = Config::getInstance().getAgentLLMConfig(role::MUSIC);
    auto providerConfig = toLLMProviderConfig(agentConfig, "poly_step_sequencer");
    logPolyStepAgentConfig(agentConfig, providerConfig);

    if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty() &&
        agentConfig.provider != provider::LLAMA_LOCAL) {
        result.error = "Poly Step Sequencer agent API key not configured.";
        result.hasError = true;
        return result;
    }

    auto client = createLLMClient(agentConfig, "poly_step_sequencer");

    llm::Request request;
    auto systemPrompt = juce::String::fromUTF8(getSystemPrompt());
    if (!deviceContext.empty())
        systemPrompt += "\n\n" + juce::String::fromUTF8(deviceContext.c_str());
    request.systemPrompt = systemPrompt;
    request.userMessage = juce::String::fromUTF8(message.c_str());
    request.temperature = 0.3f;  // some creativity for patterns

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

    logPolyStepAgentResult(result);
    return result;
}

PolyStepSequencerAgent::GenerateResult PolyStepSequencerAgent::generateStreaming(
    const std::string& message, TokenCallback onToken, const std::string& deviceContext) {
    GenerateResult result;

    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    auto agentConfig = Config::getInstance().getAgentLLMConfig(role::MUSIC);
    auto providerConfig = toLLMProviderConfig(agentConfig, "poly_step_sequencer");
    logPolyStepAgentConfig(agentConfig, providerConfig);

    if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty() &&
        agentConfig.provider != provider::LLAMA_LOCAL) {
        result.error = "Poly Step Sequencer agent API key not configured.";
        result.hasError = true;
        return result;
    }

    auto client = createLLMClient(agentConfig, "poly_step_sequencer");

    llm::Request request;
    auto systemPromptStr = juce::String::fromUTF8(getSystemPrompt());
    if (!deviceContext.empty())
        systemPromptStr += "\n\n" + juce::String::fromUTF8(deviceContext.c_str());
    request.systemPrompt = systemPromptStr;
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

    auto trimmedText = response.text.trim();
    result.rawOutput = trimmedText.toStdString();

    std::string parseError;
    result.preset = parseJson(trimmedText, parseError);
    if (!parseError.empty()) {
        result.error = parseError;
        result.hasError = true;
    }

    logPolyStepAgentResult(result);
    return result;
}

}  // namespace magda
