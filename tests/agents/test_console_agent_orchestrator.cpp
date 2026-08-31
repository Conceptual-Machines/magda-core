#include <catch2/catch_test_macros.hpp>

#include "magda/agents/console_agent_orchestrator.hpp"

using namespace magda;
using namespace magda::agent;

TEST_CASE("headless console uses the production arrangement orchestration path",
          "[console-agent-orchestrator]") {
    ConsoleAgentOrchestrator::Workflows workflows;
    std::string receivedPrompt;
    workflows.command = [&](const ConsoleRunRequest& request, const ConsoleRunObserver& observer,
                            const CancellationToken&) {
        receivedPrompt = ConsoleAgentOrchestrator::composePrompt(request);
        observer({.type = ConsoleRunEventType::Token,
                  .stream = ConsoleRunStream::Primary,
                  .text = "tracks.create"});
        ConsoleRunOutput output;
        output.dslCode = "track.new(name=\"Bass\")";
        return output;
    };

    std::vector<ConsoleRunEvent> events;
    ConsoleAgentOrchestrator orchestrator(std::move(workflows));
    const auto output =
        orchestrator.run({.surface = AgentSurfaceId::Arrangement,
                          .userMessage = "Create a bass track.",
                          .priorConversation = "Conversation so far:\nUser: Start a song.",
                          .midiContext = "Reference MIDI: C2 G2",
                          .reviseTargetClipId = INVALID_CLIP_ID},
                         [&](const ConsoleRunEvent& event) { events.push_back(event); });

    REQUIRE(output.dslCode == "track.new(name=\"Bass\")");
    REQUIRE(receivedPrompt.find("Reference MIDI: C2 G2") != std::string::npos);
    REQUIRE(receivedPrompt.find("Conversation so far") != std::string::npos);
    REQUIRE(receivedPrompt.find("User request: Create a bass track.") != std::string::npos);
    REQUIRE(events.front().type == ConsoleRunEventType::Started);
    REQUIRE(events[1].type == ConsoleRunEventType::Token);
    REQUIRE(events[1].text == "tracks.create");
    REQUIRE(events.back().type == ConsoleRunEventType::Completed);
}

TEST_CASE("console orchestration owns cancellation and terminal events",
          "[console-agent-orchestrator]") {
    ConsoleAgentOrchestrator::Workflows workflows;
    bool workflowCalled = false;
    workflows.command = [&](const ConsoleRunRequest&, const ConsoleRunObserver&,
                            const CancellationToken&) {
        workflowCalled = true;
        return ConsoleRunOutput{};
    };

    std::vector<ConsoleRunEvent> events;
    ConsoleAgentOrchestrator orchestrator(std::move(workflows));
    orchestrator.cancel();
    const auto output =
        orchestrator.run({.surface = AgentSurfaceId::Arrangement, .userMessage = "Do it."},
                         [&](const ConsoleRunEvent& event) { events.push_back(event); });

    REQUIRE(output.cancelled);
    REQUIRE_FALSE(workflowCalled);
    REQUIRE(events.size() == 2);
    REQUIRE(events.front().type == ConsoleRunEventType::Started);
    REQUIRE(events.back().type == ConsoleRunEventType::Cancelled);
}

TEST_CASE("context surfaces invoke their workflow directly without classification",
          "[console-agent-orchestrator]") {
    ConsoleAgentOrchestrator::Workflows workflows;
    int musicCalls = 0;
    workflows.music = [&](const ConsoleRunRequest& request, const ConsoleRunObserver&,
                          const CancellationToken&) {
        ++musicCalls;
        REQUIRE(request.surface == AgentSurfaceId::PianoRoll);
        ConsoleRunOutput output;
        output.musicDescription = "Context-selected piano-roll workflow";
        return output;
    };

    ConsoleAgentOrchestrator orchestrator(std::move(workflows));
    const auto output = orchestrator.run(
        {.surface = AgentSurfaceId::PianoRoll, .userMessage = "Add a passing note."});

    REQUIRE(musicCalls == 1);
    REQUIRE(output.musicDescription == "Context-selected piano-roll workflow");
}
