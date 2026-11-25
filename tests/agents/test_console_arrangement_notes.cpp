// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <string>

#include "magda/agents/console_agent_orchestrator.hpp"
#include "magda/agents/dsl_grammar.hpp"
#include "magda/daw/api/magda_api_live.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"
#include "magda/daw/project/ProjectManager.hpp"

using namespace magda;
using namespace magda::agent;

namespace {

void resetState() {
    ProjectManager::getInstance().getMutableProjectInfo() = ProjectInfo();
    ClipManager::getInstance().clearAllClips();
    TrackManager::getInstance().clearAllTracks();
    UndoManager::getInstance().clearHistory();
    SelectionManager::getInstance().clearSelection();
}

const ClipInfo* onlyClipOn(TrackId trackId) {
    auto clipIds = ClipManager::getInstance().getClipsOnTrack(trackId);
    REQUIRE(clipIds.size() == 1);
    return ClipManager::getInstance().getClip(clipIds.front());
}

bool contains(const char* text, const std::string& needle) {
    return std::string(text).find(needle) != std::string::npos;
}

}  // namespace

// #3436: the Arrange console runs only the command agent, so it must be able to write notes.
TEST_CASE("command DSL grammar and prompt let the arrangement agent write notes",
          "[console-agent-orchestrator][dsl]") {
    CHECK(contains(dsl::getGrammar(), R"g("notes" "." "add" "(" params ")")g"));
    CHECK(contains(dsl::getGrammar(), R"g("notes" "." "add_chord" "(" params ")")g"));
    CHECK(contains(dsl::getGrammar(), R"g("notes" "." "add_arpeggio" "(" params ")")g"));
    CHECK_FALSE(contains(dsl::getToolDescription(), "music agent"));
}

TEST_CASE("an arrangement reply that writes a bass line fills the clip it creates",
          "[console-agent-orchestrator][dsl]") {
    resetState();
    auto& tm = TrackManager::getInstance();
    const auto bass = tm.createTrack("Bass", TrackType::Media);
    SelectionManager::getInstance().selectTrack(bass);

    ConsoleRunOutput output;
    output.dslCode = "track(id=1).clip.new(length_bars=1)\n"
                     "  .notes.add(pitch=E2, beat=0, length=1)\n"
                     "  .notes.add(pitch=G2, beat=1, length=0.5)\n"
                     "  .notes.add(pitch=A2, beat=1.5, length=0.5)\n"
                     "  .notes.add(pitch=B2, beat=2, length=1)\n"
                     "  .notes.add(pitch=E2, beat=3, length=1)";

    MagdaApiLive api;
    const auto result = ConsoleAgentResultExecutor(api).execute(std::move(output));
    INFO(result.response);

    const auto* clip = onlyClipOn(bass);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->midiNotes.size() == 5);
    CHECK(clip->midiNotes.front().noteNumber == 40);
    CHECK(clip->midiNotes.back().startBeat == 3.0);
    resetState();
}

TEST_CASE("an arrangement reply with a chord progression on a new track fills its clip",
          "[console-agent-orchestrator][dsl]") {
    resetState();

    ConsoleRunOutput output;
    output.dslCode = "track(name=\"Keys\", new=true).clip.new(bar=1, length_bars=4)\n"
                     "  .notes.add_chord(root=C4, quality=major, beat=0, length=4)\n"
                     "  .notes.add_chord(root=F4, quality=major, beat=4, length=4)\n"
                     "  .notes.add_chord(root=G4, quality=major, beat=8, length=4)\n"
                     "  .notes.add_chord(root=C4, quality=major, beat=12, length=4)";

    MagdaApiLive api;
    const auto result = ConsoleAgentResultExecutor(api).execute(std::move(output));
    INFO(result.response);

    const auto tracks = TrackManager::getInstance().getTracks();
    REQUIRE(tracks.size() == 1);
    const auto* clip = onlyClipOn(tracks.front().id);
    REQUIRE(clip != nullptr);
    CHECK(clip->midiNotes.size() == 12);
    resetState();
}

TEST_CASE("a new track's instrument, clip and notes chain onto one track",
          "[console-agent-orchestrator][dsl]") {
    resetState();

    ConsoleRunOutput output;
    output.dslCode = "track(name=\"Lead\", new=true).fx.add(name=\"Poly Synth\")"
                     ".clip.new(bar=1, length_bars=2)"
                     ".notes.add_arpeggio(root=C4, quality=major, beat=0, step=0.5, beats=8)";

    MagdaApiLive api;
    const auto result = ConsoleAgentResultExecutor(api).execute(std::move(output));
    INFO(result.response);

    const auto tracks = TrackManager::getInstance().getTracks();
    REQUIRE(tracks.size() == 1);
    const auto* clip = onlyClipOn(tracks.front().id);
    REQUIRE(clip != nullptr);
    CHECK(clip->midiNotes.size() == 16);
    resetState();
}
