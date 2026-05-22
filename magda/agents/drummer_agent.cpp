#include "drummer_agent.hpp"

#include "../daw/audio/plugins/DrumGridRoles.hpp"
#include "../daw/core/Config.hpp"
#include "llm_client_factory.hpp"
#include "llm_presets.hpp"

namespace magda {

const char* DrummerAgent::getSystemPrompt() {
    return R"PROMPT(You are a drum pattern assistant. Output drum patterns in the grid grammar below.
Return a JSON object with exactly these fields:
  rows: a string containing only grid lines in the grammar below.
  description: a short plain-language description of the pattern, or "" if not useful.

Do not put prose, markdown, code fences, or explanations inside rows.

GRID FORMAT:
  <ROLE> | <cell> <cell> ... <cell>
- One role per line.
- Cells separated by spaces. Cell length = (bar beats) / (cells in that bar).
- Glyphs: . = rest, x = hit, X = accent.
- Use 16 cells per bar for one bar of 16th notes in 4/4 unless asked otherwise.
- For multi-bar patterns (e.g. a 4-bar phrase with a fill), separate bars with
  an extra ` | ` inside the cell list:
    K  | X . . . X . . . X . . . X . . . | X . . . X . . . X . . . X . . X
  All bars on a line MUST use the same cells-per-bar count.

ROLES (use either the short tag or the canonical id):
  K   kick
  S   snare
  SR  snare-rim
  C   clap
  HH  hh-closed
  OH  hh-open
  PH  hh-pedal
  R   ride
  RB  ride-bell
  CR  crash
  TH  tom-high
  TM  tom-mid
  TL  tom-low
  P1  perc-1
  P2  perc-2
  P3  perc-3
  P4  perc-4

Only use roles the user implies. The mapping from role to actual sound is
applied later by the host; the role names are stable, you do not need to
think about which MIDI note each one plays.

CURRENT-PATTERN CONTEXT:
If the user's prompt is preceded by one or more "Current pattern:" blocks
in grid grammar, treat them as context — those are the selected clip(s).
Use them to inform additions, variations, or fills the user is asking for.
Your output is a COMPLETE NEW PATTERN that lands in a fresh clip; it does
not merge with the existing one. If the user asks for "a fill at the end"
or "a 4-bar version with a fill", emit the full pattern including the fill
bar.

EXAMPLES:

"four-on-the-floor with closed hats"
K  | X . . . X . . . X . . . X . . .
HH | x x x x x x x x x x x x x x x x

"house with clap on 2 and 4 and open hat offbeats"
K  | X . . . X . . . X . . . X . . .
C  | . . . . X . . . . . . . X . . .
HH | x . x . x . x . x . x . x . x .
OH | . . x . . . x . . . x . . . x .

"disco — kick on every beat, open hat on the 'and'"
K  | X . . . X . . . X . . . X . . .
S  | . . . . X . . . . . . . X . . .
HH | . . x . . . x . . . x . . . x .
OH | . . . . . . . . . . . . x . . .

"boom-bap with ghost notes"
K  | X . . . . . X . . . X . . . . .
S  | . . . . X . . . . . . . X . . .
SR | . . x . . . . . . . . . . . . x
HH | x x x x X x x x x x x x X x x x

"trap hi-hats 16th feel with rolls"
K  | X . . . . . X . . . X . . . . .
S  | . . . . X . . . . . . . X . . .
HH | x x x x X x x x x x X x x x X x

"halftime breakbeat (slow snare)"
K  | X . . . . . . . . . . . . . . .
S  | . . . . . . . . X . . . . . . .
HH | x . x . x . x . x . x . x . x .

"open hat on the 'and' of 2 and 4"
K  | X . . . . . . . X . . . . . . .
S  | . . . . X . . . . . . . X . . .
HH | x . x . . . x . x . x . . . x .
OH | . . . . . . x . . . . . . . x .

"funk — 16th hats, syncopated kick, ghost snares"
K  | X . . . . . X . . . X . X . . .
S  | . . . . X . . . . . x . X . . x
HH | x x x x x x X x x x x x X x x x

"drum and bass — two-step (snare on 2 and 4)"
K  | X . . . . . . . . . X . . . . .
S  | . . . . X . . . . . . . X . . .
HH | x . x . x . x . x . x . x . x .

"drum and bass — amen-style (second snare on the + of 3, not beat 4)"
K  | X . . X . . . . . . . . . . . .
S  | . . . . X . . . . . X . . . . .
HH | x . x . x . x . x . x . x . x .

"reggae one-drop — accent on 3, no kick on 1"
K  | . . . . . . . . X . . . . . . .
S  | . . . . . . . . X . . . . . . .
SR | . . . . X . . . . . . . X . . .
HH | x . x . x . x . x . x . x . x .

"jazz swing — ride pattern with hat on 2 and 4"
K  | X . . . . . . . X . . . . . . .
SR | . . . . . . . . . . . . . . . .
R  | x . . x . x x . . x . x x . . x
PH | . . . . X . . . . . . . X . . .

"shuffle / 12/8 feel — triplet hats"
K  | X . . . . . X . . . . .
S  | . . . X . . . . . X . .
HH | x . x x . x x . x x . x

"basic rock 4/4 — 8th note hats"
K  | X . . . . . X . . . . . . . . .
S  | . . . . X . . . . . . . X . . .
HH | x . x . x . x . x . x . x . x .

"4-bar phrase ending with a tom fill"
K  | X . . . . . X . . . X . . . . . | X . . . . . X . . . X . . . . . | X . . . . . X . . . X . . . . . | X . . . X . . . . . . . . . . .
S  | . . . . X . . . . . . . X . . . | . . . . X . . . . . . . X . . . | . . . . X . . . . . . . X . . . | . . . . X . . . . . . . . . . .
HH | x x x x x x x x x x x x x x x x | x x x x x x x x x x x x x x x x | x x x x x x x x x x x x x x x x | . . . . . . . . . . . . . . . .
TH | . . . . . . . . . . . . . . . . | . . . . . . . . . . . . . . . . | . . . . . . . . . . . . . . . . | . . . . . . . . X X . . . . . .
TM | . . . . . . . . . . . . . . . . | . . . . . . . . . . . . . . . . | . . . . . . . . . . . . . . . . | . . . . . . . . . . X X . . . .
TL | . . . . . . . . . . . . . . . . | . . . . . . . . . . . . . . . . | . . . . . . . . . . . . . . . . | . . . . . . . . . . . . X X X X
CR | . . . . . . . . . . . . . . . . | . . . . . . . . . . . . . . . . | . . . . . . . . . . . . . . . . | X . . . . . . . . . . . . . . .)PROMPT";
}

namespace {
juce::var buildDrummerResponseSchema() {
    return llm::Schema::object({
        {"rows", llm::Schema::string()},
        {"description", llm::Schema::string()},
    });
}

juce::String stripMarkdownFence(juce::String text) {
    text = text.trim();
    if (!text.startsWith("```"))
        return text;

    auto firstLineEnd = text.indexOfChar('\n');
    auto lastFence = text.lastIndexOf("```");
    if (firstLineEnd >= 0 && lastFence > firstLineEnd)
        return text.substring(firstLineEnd + 1, lastFence).trim();

    return text;
}

bool isDrumGridLine(const juce::String& line) {
    if (!line.containsChar('|'))
        return false;

    auto roleToken = line.upToFirstOccurrenceOf("|", false, false).trim();
    return daw::audio::drum_grid_roles::roleIdForToken(roleToken).isNotEmpty();
}

juce::String extractLastGridBlock(const juce::String& text) {
    juce::StringArray currentBlock;
    juce::StringArray lastBlock;

    juce::StringArray lines;
    lines.addLines(text);

    for (auto line : lines) {
        line = line.trim();

        if (isDrumGridLine(line)) {
            currentBlock.add(line);
            continue;
        }

        if (!currentBlock.isEmpty()) {
            lastBlock = currentBlock;
            currentBlock.clear();
        }
    }

    if (!currentBlock.isEmpty())
        lastBlock = currentBlock;

    return lastBlock.joinIntoString("\n").trim();
}

juce::String extractRowsFromResponse(const juce::String& rawText, juce::String& description) {
    auto text = stripMarkdownFence(rawText);

    auto parsed = juce::JSON::parse(text);
    if (auto* obj = parsed.getDynamicObject()) {
        auto rows = obj->getProperty("rows");
        if (rows.isString()) {
            if (auto desc = obj->getProperty("description"); desc.isString())
                description = desc.toString().trim();

            auto rowsText = rows.toString().trim();
            if (rowsText.isNotEmpty())
                return rowsText;
        }
    }

    return extractLastGridBlock(text);
}

void parseResponse(CompactParser& parser, DrummerAgent::GenerateResult& result,
                   const juce::String& rawText) {
    auto trimmedText = rawText.trim();
    result.rawOutput = trimmedText.toStdString();

    juce::String description;
    auto rowsText = extractRowsFromResponse(trimmedText, description);
    result.description = description.toStdString();

    if (rowsText.isEmpty()) {
        result.error = "Parse error: no drum grid rows found";
        result.hasError = true;
        return;
    }

    result.instructions = parser.parse(rowsText);
    if (result.instructions.empty() && parser.getLastError().isNotEmpty()) {
        result.error = "Parse error: " + parser.getLastError().toStdString();
        result.hasError = true;
    }
}

void logResult(const DrummerAgent::GenerateResult& result) {
    const auto& rawOutput = result.rawOutput;
    DBG("MAGDA DrummerAgent raw output (" + juce::String(static_cast<int>(rawOutput.size())) +
        " chars):");
    DBG("---8<---");
    DBG(juce::String(rawOutput));
    DBG("--->8---");
    if (!result.description.empty())
        DBG("MAGDA DrummerAgent description: " + juce::String(result.description));
    DBG("MAGDA DrummerAgent parsed " + juce::String(static_cast<int>(result.instructions.size())) +
        " instruction(s)");
    if (!result.error.empty())
        DBG("MAGDA DrummerAgent ERROR: " + juce::String(result.error));
}
}  // namespace

DrummerAgent::GenerateResult DrummerAgent::generate(const std::string& message) {
    GenerateResult result;

    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    // Drummer reuses the MUSIC LLM slot — same family of model is appropriate
    // and avoids forcing users to configure a fourth role.
    auto agentConfig = Config::getInstance().getAgentLLMConfig(role::MUSIC);
    auto providerConfig = toLLMProviderConfig(agentConfig, "drummer");

    if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty() &&
        agentConfig.provider != provider::LLAMA_LOCAL) {
        result.error = "Drummer agent API key not configured.";
        result.hasError = true;
        return result;
    }

    auto client = createLLMClient(agentConfig, "drummer");

    llm::Request request;
    request.systemPrompt = juce::String::fromUTF8(getSystemPrompt());
    request.userMessage = juce::String::fromUTF8(message.c_str());
    request.temperature = 0.4f;
    request.schema = buildDrummerResponseSchema();

    auto response = client->sendRequest(request);
    if (!response.success) {
        result.error = response.error.toStdString();
        result.hasError = true;
        return result;
    }

    parseResponse(parser_, result, response.text);
    logResult(result);
    return result;
}

DrummerAgent::GenerateResult DrummerAgent::generateStreaming(const std::string& message,
                                                             TokenCallback onToken) {
    GenerateResult result;

    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    auto agentConfig = Config::getInstance().getAgentLLMConfig(role::MUSIC);
    auto providerConfig = toLLMProviderConfig(agentConfig, "drummer");

    if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty() &&
        agentConfig.provider != provider::LLAMA_LOCAL) {
        result.error = "Drummer agent API key not configured.";
        result.hasError = true;
        return result;
    }

    auto client = createLLMClient(agentConfig, "drummer");

    llm::Request request;
    request.systemPrompt = juce::String::fromUTF8(getSystemPrompt());
    request.userMessage = juce::String::fromUTF8(message.c_str());
    request.temperature = 0.4f;
    request.schema = buildDrummerResponseSchema();

    auto response =
        client->sendStreamingRequest(request, [this, &onToken](const juce::String& tok) {
            if (shouldStop_.load())
                return false;
            return onToken ? onToken(tok) : true;
        });

    if (!response.success) {
        result.error = response.error.toStdString();
        result.hasError = true;
        return result;
    }

    parseResponse(parser_, result, response.text);
    logResult(result);
    return result;
}

}  // namespace magda
