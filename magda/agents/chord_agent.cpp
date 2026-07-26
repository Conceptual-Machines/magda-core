#include "chord_agent.hpp"

#include <algorithm>
#include <atomic>

#include "../daw/core/LLMClientProvider.hpp"

namespace magda {
namespace {

constexpr const char* kChordProgressionGrammar = R"GRAMMAR(
start: progression+

progression: "progression(" params ")" chord_chain

chord_chain: chord+

chord: ".add_chord(" params ")"

params: param ("," param)*

param: IDENTIFIER "=" value

value: STRING
     | NUMBER
     | IDENTIFIER

STRING: "\"" /[^"]*/ "\""
NUMBER: /-?[0-9]+(\.[0-9]+)?/
IDENTIFIER: /[a-zA-Z_#][a-zA-Z0-9_#]*/

%import common.WS
%ignore WS
)GRAMMAR";

constexpr const char* kChordToolDescription =
    "Generates chord progression suggestions using MAGDA DSL.\n\n"
    "Format: progression(name=\"Name\", description=\"Why it works\") followed by "
    ".add_chord(root=<note>, quality=<quality>, beat=<beat>, length=<beats>, "
    "inversion=<0|1|2>) chains.\n\n"
    "Example:\n"
    "progression(name=\"Classic Pop\", description=\"Timeless I-V-vi-IV cadence\")"
    ".add_chord(root=C4, quality=major, beat=0, length=1)"
    ".add_chord(root=G3, quality=major, beat=1, length=1, inversion=1)"
    ".add_chord(root=A3, quality=minor, beat=2, length=1)"
    ".add_chord(root=F3, quality=major, beat=3, length=1, inversion=2)\n"
    "progression(name=\"Jazz ii-V-I\", description=\"Smooth jazz resolution\")"
    ".add_chord(root=D3, quality=min7, beat=0, length=1)"
    ".add_chord(root=G3, quality=dom7, beat=1, length=1, inversion=1)"
    ".add_chord(root=C4, quality=maj7, beat=2, length=2)\n\n"
    "Qualities: major, minor, dim, aug, dom7, maj7, min7, dim7, dom9, maj9, min9, "
    "sus2, sus4, add9, madd9, 6, min6, power\n"
    "Inversion: 0=root position (default), 1=first inversion, 2=second inversion. "
    "Use inversions to create smooth voice leading matching the played voicings.\n"
    "Notes: C3-B5 range (e.g. C4, F#3, Bb4)";

constexpr const char* kChordToolDescriptionLocal =
    "Generate chord progressions using MAGDA DSL. No prose.\n\n"
    "Format: progression() followed by .add_chord() chains. 4-8 chords per progression.\n\n"
    "Example:\n"
    "progression()"
    ".add_chord(root=C4, quality=major, beat=0, length=1)"
    ".add_chord(root=G3, quality=major, beat=1, length=1)"
    ".add_chord(root=A3, quality=minor, beat=2, length=1)"
    ".add_chord(root=F3, quality=major, beat=3, length=1)\n"
    "progression()"
    ".add_chord(root=D3, quality=min7, beat=0, length=1)"
    ".add_chord(root=G3, quality=dom7, beat=1, length=1)"
    ".add_chord(root=C4, quality=maj7, beat=2, length=2)";

juce::String buildContext(const ChordAgent::Input& input) {
    juce::String context;
    if (input.chordTrackProgression.isNotEmpty())
        context += "Chord-track progression: " + input.chordTrackProgression + "\n";
    if (input.detectedKey.isNotEmpty())
        context += "Detected key: " + input.detectedKey + "\n";
    if (!input.recentChords.empty()) {
        juce::StringArray history;
        for (const auto& chord : input.recentChords)
            history.add(chord);
        context += "Recent chord history: " + history.joinIntoString(", ") + "\n";
    }
    if (!input.detectedScales.empty()) {
        juce::StringArray scales;
        const auto count = std::min(input.detectedScales.size(), size_t{3});
        for (size_t i = 0; i < count; ++i)
            scales.add(input.detectedScales[i]);
        context += "Detected scales: " + scales.joinIntoString(", ") + "\n";
    }
    return context;
}

juce::String paramValue(const juce::String& params, const juce::String& key) {
    const auto marker = key + "=";
    int start = params.indexOf(marker);
    if (start < 0)
        return {};
    start += marker.length();
    const int end = params.indexOf(start, ",");
    return (end >= 0 ? params.substring(start, end) : params.substring(start)).trim();
}

}  // namespace

ChordAgent::RequestPlan ChordAgent::buildRequest(const Input& input,
                                                 const Config::AgentLLMConfig& agentConfig) {
    RequestPlan plan;
    plan.agentConfig = agentConfig;
    plan.usesLocalPrompt = agentConfig.provider == provider::LLAMA_LOCAL;
    plan.usesCfg = supportsOpenAICFG(agentConfig);
    plan.usesStreaming = !plan.usesCfg;

    const auto context = buildContext(input);
    if (input.userPrompt.isNotEmpty()) {
        plan.request.userMessage = input.userPrompt + "\n\n";
        if (context.isNotEmpty())
            plan.request.userMessage += "Musical context:\n" + context + "\n";
    } else {
        plan.request.userMessage =
            "Based on this musical context, suggest 3-4 chord progressions as "
            "continuations or variations.\n\n" +
            context + "\n";
    }

    if (plan.usesLocalPrompt) {
        plan.request.userMessage +=
            "Generate chord progressions. Each progression() block should have 4-8 chords "
            "via .add_chord() calls.\n"
            "Use beat values starting at 0, incrementing by the chord length.\n"
            "Use appropriate octaves (root around C3-C4).\n"
            "Quality names: major, minor, dim, aug, dom7, maj7, min7, dim7, dom9, maj9, "
            "min9, sus2, sus4, add9, madd9, 6, min6, power.";
    } else {
        plan.request.userMessage +=
            "Generate DSL using the chord_progressions tool. Each progression() block should "
            "have a name, a short description (under 60 chars), and 4-8 chords via "
            ".add_chord() calls.\n"
            "Use beat values starting at 0, incrementing by the chord length.\n"
            "Use appropriate octaves (root around C3-C4). Use inversions (inversion=0/1/2) "
            "to match the voicings in the chord history and create smooth voice leading.\n"
            "Quality names: major, minor, dim, aug, dom7, maj7, min7, dim7, dom9, maj9, "
            "min9, sus2, sus4, add9, madd9, 6, min6, power.";
    }

    plan.request.systemPrompt =
        plan.usesLocalPrompt ? kChordToolDescriptionLocal : kChordToolDescription;
    plan.request.temperature = 0.1f;
    if (plan.usesCfg) {
        plan.request.grammar = kChordProgressionGrammar;
        plan.request.grammarToolName = "chord_progression";
        plan.request.grammarToolDescription = kChordToolDescription;
    }
    return plan;
}

ChordAgent::Result ChordAgent::parseResponse(const juce::String& dsl) {
    Result result;
    result.rawOutput = dsl;
    if (dsl.trim().isEmpty()) {
        result.hasError = true;
        result.error = "Chord agent returned empty output.";
        return result;
    }

    auto& engine = music::ChordEngine::getInstance();
    int pos = 0;
    int malformedBlocks = 0;
    while (pos < dsl.length()) {
        const int progStart = dsl.indexOf(pos, "progression(");
        if (progStart < 0)
            break;

        const int nextProg = dsl.indexOf(progStart + 12, "progression(");
        const auto progBlock =
            nextProg >= 0 ? dsl.substring(progStart, nextProg) : dsl.substring(progStart);
        Progression progression;

        if (int start = progBlock.indexOf("name=\""); start >= 0) {
            start += 6;
            if (const int end = progBlock.indexOf(start, "\""); end >= 0)
                progression.name = progBlock.substring(start, end);
        }
        if (int start = progBlock.indexOf("description=\""); start >= 0) {
            start += 13;
            if (const int end = progBlock.indexOf(start, "\""); end >= 0)
                progression.description = progBlock.substring(start, end);
        }

        int chordPos = 0;
        while (chordPos < progBlock.length()) {
            const int chordStart = progBlock.indexOf(chordPos, ".add_chord(");
            if (chordStart < 0)
                break;
            const int paramsStart = chordStart + 11;
            const int paramsEnd = progBlock.indexOf(paramsStart, ")");
            if (paramsEnd < 0) {
                ++malformedBlocks;
                break;
            }

            const auto params = progBlock.substring(paramsStart, paramsEnd);
            auto root = paramValue(params, "root");
            const auto quality = paramValue(params, "quality");
            int octave = 4;
            if (root.isNotEmpty()) {
                const auto last = root[root.length() - 1];
                if (last >= '0' && last <= '9') {
                    octave = last - '0';
                    root = root.dropLastCharacters(1);
                }
            }
            const int inversion = paramValue(params, "inversion").getIntValue();

            if (root.isNotEmpty() && quality.isNotEmpty()) {
                const auto spec = engine.parseChordName(root + " " + quality);
                auto chord =
                    inversion > 0
                        ? engine.buildChordInversion(spec.root, spec.quality, inversion, octave)
                        : engine.buildChordInRootPosition(spec.root, spec.quality, octave);
                music::ChordEngine::finalizeChord(chord);
                if (!chord.notes.empty())
                    progression.chords.push_back(std::move(chord));
            } else {
                ++malformedBlocks;
            }
            chordPos = paramsEnd + 1;
        }

        if (!progression.chords.empty())
            result.progressions.push_back(std::move(progression));
        else
            ++malformedBlocks;
        pos = nextProg >= 0 ? nextProg : dsl.length();
    }

    if (result.progressions.empty()) {
        result.hasError = true;
        result.error = malformedBlocks > 0
                           ? "Chord agent output contained no valid progression blocks."
                           : "Chord agent output did not contain progression() DSL.";
    }
    return result;
}

ChordAgent::Result ChordAgent::generate(const Input& input, TokenCallback onToken,
                                        CancelCallback shouldCancel) const {
    const auto cancelled = [&] { return shouldCancel && shouldCancel(); };
    if (cancelled())
        return {{}, {}, "Cancelled", true, true};

    const auto profile = Config::getInstance().getAgentInferenceConfig(role::CHORD);
    if (!profile.usesLLM()) {
        Result result;
        result.error = "Chord agent's configured inference backend is not supported yet.";
        result.hasError = true;
        return result;
    }
    auto plan = buildRequest(input, profile.llm);
    auto client = createLLMClient(plan.agentConfig, "chord");
    if (!client)
        return {{}, {}, "Could not create Chord agent LLM client.", true, false};

    std::atomic<bool> streamCancelled{false};
    llm::Response response;
    if (plan.usesCfg) {
        response = client->sendRequest(plan.request);
    } else {
        response = client->sendStreamingRequest(plan.request, [&](const juce::String& token) {
            if (cancelled()) {
                streamCancelled = true;
                return false;
            }
            if (onToken && !onToken(token)) {
                streamCancelled = true;
                return false;
            }
            return true;
        });
    }

    if (cancelled() || streamCancelled.load())
        return {{}, response.text, "Cancelled", true, true};
    if (!response.success) {
        Result result;
        result.rawOutput = response.text;
        result.error = response.error.isNotEmpty() ? response.error : "Chord agent request failed.";
        result.hasError = true;
        return result;
    }
    return parseResponse(response.text.trim());
}

}  // namespace magda
