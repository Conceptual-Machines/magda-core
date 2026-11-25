// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @brief Live-model evals for the Arrange console (#3436): each request goes to a real
 * LLM through the production orchestrator, and the clips it leaves behind are checked.
 *
 * Hidden tag, so `make test` never runs them. `make eval` needs MAGDA_EVAL_API_KEY;
 * MAGDA_EVAL_PROVIDER, MAGDA_EVAL_MODEL and MAGDA_EVAL_BASE_URL pick the backend
 * (default Anthropic Haiku). CI: .github/workflows/evals.yml.
 */

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <string>
#include <vector>

#include "magda/agents/automation_agent.hpp"
#include "magda/agents/command_agent.hpp"
#include "magda/agents/console_agent_orchestrator.hpp"
#include "magda/agents/drummer_agent.hpp"
#include "magda/agents/music_agent.hpp"
#include "magda/daw/api/magda_api_live.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/Config.hpp"
#include "magda/daw/core/LLMClientProvider.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"
#include "magda/daw/project/ProjectManager.hpp"

using namespace magda;
using namespace magda::agent;

namespace {

std::string envOr(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? value : fallback;
}

void resetState() {
    ProjectManager::getInstance().getMutableProjectInfo() = ProjectInfo();
    ClipManager::getInstance().clearAllClips();
    TrackManager::getInstance().clearAllTracks();
    UndoManager::getInstance().clearHistory();
    SelectionManager::getInstance().clearSelection();
}

struct ClipSummary {
    TrackId trackId = INVALID_TRACK_ID;
    size_t notes = 0;
    bool notesInsideClip = true;
};

std::vector<ClipSummary> midiClips() {
    std::vector<ClipSummary> out;
    auto& cm = ClipManager::getInstance();
    for (const auto& track : TrackManager::getInstance().getTracks()) {
        for (auto clipId : cm.getClipsOnTrack(track.id)) {
            const auto* clip = cm.getClip(clipId);
            if (clip == nullptr || !clip->isMidi())
                continue;
            ClipSummary summary{.trackId = track.id, .notes = clip->midiNotes.size()};
            for (const auto& note : clip->midiNotes)
                if (note.startBeat < 0.0 || note.startBeat >= clip->placement.lengthBeats)
                    summary.notesInsideClip = false;
            out.push_back(summary);
        }
    }
    return out;
}

/** Runs @p message on the Arrange surface with a "Bass" track selected, then executes it. */
ConsoleExecutionResult runArrangeRequest(MagdaApi& api, const std::string& message,
                                         TrackId& selectedTrack, std::string& dsl) {
    resetState();
    selectedTrack = TrackManager::getInstance().createTrack("Bass", TrackType::Media);
    SelectionManager::getInstance().selectTrack(selectedTrack);

    CommandAgent command(api);
    MusicAgent music;
    AutomationAgent automation(api);
    DrummerAgent drummer;
    ConsoleAgentOrchestrator orchestrator(command, music, automation, drummer);
    auto output =
        orchestrator.run({.surface = AgentSurfaceId::Arrangement, .userMessage = message});
    dsl = output.dslCode;
    UNSCOPED_INFO("request: " << message);
    UNSCOPED_INFO("dsl:\n" << dsl);
    if (!output.error.empty())
        UNSCOPED_INFO("agent error: " << output.error);
    return ConsoleAgentResultExecutor(api).execute(std::move(output));
}

}  // namespace

TEST_CASE("eval: Arrange console requests for music leave notes in the clip",
          "[.eval][console-agent-orchestrator]") {
    const auto apiKey = envOr("MAGDA_EVAL_API_KEY", "");
    if (apiKey.empty())
        SKIP("MAGDA_EVAL_API_KEY is not set");

    Config::getInstance().setAgentLLMConfig(
        role::COMMAND, {.provider = envOr("MAGDA_EVAL_PROVIDER", provider::ANTHROPIC),
                        .baseUrl = envOr("MAGDA_EVAL_BASE_URL", ""),
                        .apiKey = apiKey,
                        .model = envOr("MAGDA_EVAL_MODEL", model::CLAUDE_HAIKU)});

    MagdaApiLive api;
    TrackId selected = INVALID_TRACK_ID;
    std::string dsl;

    SECTION("a bass line goes on the selected track") {
        auto result =
            runArrangeRequest(api, "add a bass line to the selected track", selected, dsl);
        INFO(result.response);
        auto clips = midiClips();
        REQUIRE(clips.size() >= 1);
        for (const auto& clip : clips) {
            CHECK(clip.trackId == selected);
            CHECK(clip.notes > 0);
            CHECK(clip.notesInsideClip);
        }
    }

    SECTION("a chord progression has notes") {
        auto result =
            runArrangeRequest(api, "write a four bar chord progression in A minor", selected, dsl);
        INFO(result.response);
        auto clips = midiClips();
        REQUIRE(clips.size() >= 1);
        for (const auto& clip : clips) {
            CHECK(clip.notes >= 6);
            CHECK(clip.notesInsideClip);
        }
    }

    SECTION("a new track with an arpeggio has notes") {
        auto result = runArrangeRequest(api, "create a new Lead track with a C major arpeggio",
                                        selected, dsl);
        INFO(result.response);
        CHECK(TrackManager::getInstance().getTracks().size() == 2);
        auto clips = midiClips();
        REQUIRE(clips.size() >= 1);
        for (const auto& clip : clips) {
            CHECK(clip.trackId != selected);
            CHECK(clip.notes > 0);
        }
    }

    SECTION("a drum beat has notes") {
        auto result = runArrangeRequest(api, "add a two bar drum beat", selected, dsl);
        INFO(result.response);
        auto clips = midiClips();
        REQUIRE(clips.size() >= 1);
        for (const auto& clip : clips)
            CHECK(clip.notes >= 4);
    }

    SECTION("an empty clip request stays empty") {
        auto result =
            runArrangeRequest(api, "add an empty 4 bar clip to the selected track", selected, dsl);
        INFO(result.response);
        auto clips = midiClips();
        REQUIRE(clips.size() == 1);
        CHECK(clips.front().notes == 0);
    }

    resetState();
}
