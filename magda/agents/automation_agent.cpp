#include "automation_agent.hpp"

#include "../daw/core/AutomationManager.hpp"
#include "../daw/core/Config.hpp"
#include "../daw/core/ParameterInfo.hpp"
#include "../daw/core/SelectionManager.hpp"
#include "../daw/core/TrackManager.hpp"
#include "llm_client_factory.hpp"

namespace magda {

const char* AutomationAgent::getSystemPrompt() {
    return R"PROMPT(You are the MAGDA Automation agent. You write automation curves on a lane.

Respond ONLY with AUTO instructions. No prose. No markdown. No backticks.
One instruction per line.

TIME is in BEATS. VALUES are NORMALIZED in [0.0, 1.0] where 0.0 = parameter
minimum and 1.0 = parameter maximum. The user may speak in real units
("filter from 200Hz to 2kHz") but you emit normalized values; the DAW maps
them back to the real parameter range.

A BAR = 4 BEATS. "over 2 bars" = span 8 beats. "8 bars" = span 32 beats.

INSTRUCTIONS:
  AUTO sin    start=<beat> end=<beat> min=<0..1> max=<0..1> cycles=<N>
  AUTO tri    start=<beat> end=<beat> min=<0..1> max=<0..1> cycles=<N>
  AUTO saw    start=<beat> end=<beat> min=<0..1> max=<0..1> cycles=<N>
  AUTO square start=<beat> end=<beat> min=<0..1> max=<0..1> cycles=<N> [duty=<0..1>]
  AUTO exp    start=<beat> end=<beat> min=<0..1> max=<0..1>
  AUTO log    start=<beat> end=<beat> min=<0..1> max=<0..1>
  AUTO line   start=<beat> end=<beat> from=<0..1> to=<0..1>
  AUTO freeform points=(<beat>,<0..1>)(<beat>,<0..1>)...
  AUTO clear

TARGETS (append ` target=<...>` to any instruction):
  target=volume     — currently selected track's volume fader (DEFAULT for
                      "volume", "fade", "fade in", "fade out", "tremolo")
  target=pan        — currently selected track's pan knob (DEFAULT for
                      "pan", "auto pan")
  target=selected   — currently selected automation lane (DEFAULT for
                      any request that's clearly about a chosen parameter
                      like "filter cutoff", "reverb mix")
  target=laneId:<N> — a specific lane by id (see context)

If the user does not specify a target, PICK THE RIGHT DEFAULT from the list
above based on the words they used. Do not refuse.

EXAMPLES:
"8 bar volume fade in" ->
AUTO line start=0 end=32 from=0 to=1 target=volume

"volume fade out over 4 bars" ->
AUTO line start=0 end=16 from=1 to=0 target=volume

"tremolo, 8 cycles over 2 bars" ->
AUTO sin start=0 end=8 min=0.3 max=1 cycles=8 target=volume

"auto pan, slow 2-cycle sweep over 4 bars" ->
AUTO sin start=0 end=16 min=0 max=1 cycles=2 target=pan

"slow filter sweep up over 4 bars" ->
AUTO line start=0 end=16 from=0 to=1 target=selected

"sine LFO, 4 cycles over 2 bars" ->
AUTO sin start=0 end=8 min=0 max=1 cycles=4 target=selected

"clear the automation and draw a rising saw, 2 cycles over 4 bars" ->
AUTO clear target=selected
AUTO saw start=0 end=16 min=0 max=1 cycles=2 target=selected

"custom shape: start low, jump high at beat 2, ramp down to zero at beat 8" ->
AUTO freeform points=(0,0.1)(2,0.9)(8,0) target=selected)PROMPT";
}

namespace {

/** Multi-line selection context appended to the system prompt. */
juce::String buildSelectionContext() {
    auto& sel = SelectionManager::getInstance();
    auto& amgr = AutomationManager::getInstance();
    auto& tmgr = TrackManager::getInstance();
    juce::String out;

    TrackId contextTrackId = sel.getSelectedTrack();

    if (sel.hasAutomationLaneSelection()) {
        auto laneId = sel.getAutomationLaneSelection().laneId;
        if (auto* lane = amgr.getLane(laneId)) {
            auto info = lane->target.getParameterInfo();
            out << "Selected lane: \"" << lane->getDisplayName() << "\" (laneId=" << laneId
                << ", range " << info.minValue << ".." << info.maxValue;
            if (info.unit.isNotEmpty())
                out << " " << info.unit;
            out << ").\n";
            if (contextTrackId == INVALID_TRACK_ID)
                contextTrackId = lane->target.trackId;
        }
    } else {
        out << "No automation lane is currently selected.\n";
    }

    if (contextTrackId != INVALID_TRACK_ID) {
        if (const auto* track = tmgr.getTrack(contextTrackId)) {
            out << "Selected track: \"" << track->name << "\" (trackId=" << contextTrackId << "). "
                << "target=volume and target=pan resolve against this track.";
        }
    } else {
        out << "No track is currently selected.";
    }

    return out;
}

/** Strip markdown fences + prose if the model wraps the AUTO block. */
std::string cleanOutput(const juce::String& raw) {
    auto text = raw.trim();
    if (text.contains("```")) {
        auto start = text.indexOf("```");
        auto afterFence = text.indexOf(start, "\n");
        if (afterFence < 0)
            afterFence = start + 3;
        else
            afterFence += 1;
        auto end = text.lastIndexOf("```");
        if (end > start)
            text = text.substring(afterFence, end).trim();
    }
    return text.toStdString();
}

llm::Request buildRequest(const std::string& message) {
    auto systemPrompt = juce::String::fromUTF8(AutomationAgent::getSystemPrompt());
    auto ctx = buildSelectionContext();
    if (ctx.isNotEmpty())
        systemPrompt += "\n\nContext:\n" + ctx;

    llm::Request request;
    request.systemPrompt = systemPrompt;
    request.userMessage = juce::String::fromUTF8(message.c_str());
    request.temperature = 0.1f;
    return request;
}

}  // namespace

AutomationAgent::GenerateResult AutomationAgent::generate(const std::string& message) {
    GenerateResult result;

    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    // Reuse the COMMAND role config — automation is a DAW-modifying intent.
    auto agentConfig = Config::getInstance().getAgentLLMConfig(role::COMMAND);

    if (agentConfig.provider != provider::LLAMA_LOCAL) {
        auto providerConfig = toLLMProviderConfig(agentConfig);
        if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty()) {
            result.error = "Automation agent API key not configured.";
            result.hasError = true;
            return result;
        }
    }

    auto client = createLLMClient(agentConfig, "automation");
    auto request = buildRequest(message);

    auto response = client->sendRequest(request);

    if (!response.success) {
        DBG("MAGDA AutomationAgent ERROR (" + client->getName() + "/" + client->getConfig().model +
            "): " + response.error);
        result.error = response.error.toStdString();
        result.hasError = true;
        return result;
    }

    result.rawOutput = cleanOutput(response.text);

    DBG("MAGDA AutomationAgent (" + client->getName() + "/" + client->getConfig().model + ", " +
        juce::String(response.wallSeconds, 2) + "s): " + juce::String(result.rawOutput));

    result.instructions = parser_.parse(juce::String(result.rawOutput));
    if (result.instructions.empty() && parser_.getLastError().isNotEmpty()) {
        result.error = "Parse error: " + parser_.getLastError().toStdString();
        result.hasError = true;
    }

    return result;
}

AutomationAgent::GenerateResult AutomationAgent::generateStreaming(const std::string& message,
                                                                   TokenCallback onToken) {
    GenerateResult result;

    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    auto agentConfig = Config::getInstance().getAgentLLMConfig(role::COMMAND);

    if (agentConfig.provider != provider::LLAMA_LOCAL) {
        auto providerConfig = toLLMProviderConfig(agentConfig);
        if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty()) {
            result.error = "Automation agent API key not configured.";
            result.hasError = true;
            return result;
        }
    }

    auto client = createLLMClient(agentConfig, "automation");
    auto request = buildRequest(message);

    auto response = client->sendStreamingRequest(request, [&](const juce::String& token) {
        if (shouldStop_.load())
            return false;
        if (onToken)
            return onToken(token);
        return true;
    });

    if (!response.success) {
        DBG("MAGDA AutomationAgent stream ERROR (" + client->getName() + "/" +
            client->getConfig().model + "): " + response.error);
        result.error = response.error.toStdString();
        result.hasError = true;
        return result;
    }

    result.rawOutput = cleanOutput(response.text);

    DBG("MAGDA AutomationAgent stream (" + client->getName() + "/" + client->getConfig().model +
        ", " + juce::String(response.wallSeconds, 2) + "s): " + juce::String(result.rawOutput));

    result.instructions = parser_.parse(juce::String(result.rawOutput));
    if (result.instructions.empty() && parser_.getLastError().isNotEmpty()) {
        result.error = "Parse error: " + parser_.getLastError().toStdString();
        result.hasError = true;
    }

    return result;
}

std::string AutomationAgent::execute(const GenerateResult& result) {
    if (result.hasError)
        return result.error;

    if (!executor_.execute(result.instructions)) {
        return "Automation execution error: " + executor_.getError().toStdString() +
               "\nAUTO was: " + result.rawOutput;
    }

    auto results = executor_.getResults();
    if (results.isEmpty())
        return "Done.";

    return results.toStdString();
}

}  // namespace magda
