#include <algorithm>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/api/magda_api_live.hpp"
#include "magda/daw/api/remote_handlers.hpp"
#include "magda/daw/api/session_api_live.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/UndoManager.hpp"
#include "magda/daw/project/ProjectManager.hpp"

using namespace magda;

namespace {

struct RestoreSessionState {
    SessionApiLive api;
    SessionSceneState original = api.captureSceneState();

    ~RestoreSessionState() {
        api.restoreSceneState(original);
    }
};

int indexOf(const std::vector<ProjectScene>& scenes, SceneId id) {
    const auto found = std::ranges::find(scenes, id, &ProjectScene::id);
    return found == scenes.end() ? -1 : static_cast<int>(found - scenes.begin());
}

juce::var object(std::initializer_list<std::pair<const char*, juce::var>> fields) {
    auto* result = new juce::DynamicObject();
    for (const auto& [key, value] : fields)
        result->setProperty(key, value);
    return result;
}

}  // namespace

TEST_CASE("Session scene lifecycle preserves stable ids and remaps every occupied slot",
          "[remote][session][scenes][2842]") {
    RestoreSessionState fixture;
    fixture.api.restoreSceneState(
        {{{10, "Intro", 0xFF010203}, {20, "Drop", 0xFF040506}, {30, "Outro", 0xFF070809}}, 31, {}});

    auto& clips = ClipManager::getInstance();
    const auto clipId = clips.createMidiClipBeats(4242, 0.0, 4.0, ClipView::Session);
    REQUIRE(clipId != INVALID_CLIP_ID);
    clips.setClipSceneIndex(clipId, 1);

    const auto inserted = fixture.api.createScene(1, "Break", 0xFF112233);
    REQUIRE(inserted == 31);
    CHECK(clips.getClip(clipId)->sceneIndex == 2);

    REQUIRE(fixture.api.updateScene(inserted, "Breakdown", 0xFF445566));
    const auto& updated = ProjectManager::getInstance().getCurrentProjectInfo().scenes[1];
    CHECK(updated.name == "Breakdown");
    CHECK(updated.colourArgb == 0xFF445566);

    REQUIRE(fixture.api.moveScene(20, 0));
    CHECK(clips.getClip(clipId)->sceneIndex == 0);

    const auto duplicate = fixture.api.duplicateScene(20, true);
    REQUIRE(duplicate == 32);
    const auto copiedClip = clips.getClipInSlot(4242, 1);
    REQUIRE(copiedClip != INVALID_CLIP_ID);
    CHECK(copiedClip != clipId);

    CHECK_FALSE(fixture.api.deleteScene(20, PopulatedScenePolicy::Fail, INVALID_SCENE_ID));
    REQUIRE(fixture.api.deleteScene(20, PopulatedScenePolicy::MoveClips, 30));
    const auto& scenes = ProjectManager::getInstance().getCurrentProjectInfo().scenes;
    CHECK(indexOf(scenes, 20) == -1);
    CHECK(clips.getClip(clipId)->sceneIndex == indexOf(scenes, 30));
    CHECK(clips.getClip(copiedClip)->sceneIndex == indexOf(scenes, duplicate));

    REQUIRE(
        fixture.api.deleteScene(duplicate, PopulatedScenePolicy::DeleteClips, INVALID_SCENE_ID));
    CHECK(clips.getClip(copiedClip) == nullptr);
}

TEST_CASE("Duplicating a populated scene is one stable undoable command",
          "[remote][session][scenes][undo][2842]") {
    remote::ScopedMessageThreadAssertionDisabler threadAssertionGuard;
    RestoreSessionState fixture;
    fixture.api.restoreSceneState({{{10, "Intro", 0}, {20, "Drop", 0}}, 21, {}});

    auto& clips = ClipManager::getInstance();
    const auto sourceClip = clips.createMidiClipBeats(4242, 0.0, 4.0, ClipView::Session);
    clips.setClipSceneIndex(sourceClip, 0);

    auto& undo = UndoManager::getInstance();
    undo.clearHistory();
    MagdaApiLive api;
    const auto result = remote::handlers::sessionDuplicateScene(
        api, object({{"sceneId", 10}, {"copyClips", true}}), {});
    REQUIRE_FALSE(result.failed());

    const auto duplicatedScene = ProjectManager::getInstance().getCurrentProjectInfo().scenes[1].id;
    const auto duplicatedClip = clips.getClipInSlot(4242, 1);
    REQUIRE(duplicatedScene == 21);
    REQUIRE(duplicatedClip != INVALID_CLIP_ID);

    REQUIRE(undo.undo());
    CHECK(indexOf(ProjectManager::getInstance().getCurrentProjectInfo().scenes, duplicatedScene) ==
          -1);
    CHECK(clips.getClip(duplicatedClip) == nullptr);

    REQUIRE(undo.redo());
    CHECK(ProjectManager::getInstance().getCurrentProjectInfo().scenes[1].id == duplicatedScene);
    CHECK(clips.getClipInSlot(4242, 1) == duplicatedClip);

    // The command owns a reference into the local MagdaApiLive facade.
    undo.clearHistory();
}

TEST_CASE("Session launch settings update and undo as one clip snapshot",
          "[remote][session][settings][undo][2848]") {
    remote::ScopedMessageThreadAssertionDisabler threadAssertionGuard;
    RestoreSessionState fixture;
    auto& clips = ClipManager::getInstance();
    const auto clipId = clips.createMidiClipBeats(4242, 0.0, 4.0, ClipView::Session);
    REQUIRE(clipId != INVALID_CLIP_ID);
    clips.setClipSceneIndex(clipId, 0);

    auto& undo = UndoManager::getInstance();
    undo.clearHistory();
    MagdaApiLive api;
    const auto result =
        remote::handlers::sessionUpdateClipSettings(api,
                                                    object({{"clipId", clipId},
                                                            {"launchMode", "toggle"},
                                                            {"launchQuantize", "1/16"},
                                                            {"followAction", "again"},
                                                            {"followActionDelayBeats", 2.0},
                                                            {"followActionLoopCount", 3}}),
                                                    {});
    REQUIRE_FALSE(result.failed());
    const auto* updated = clips.getClip(clipId);
    REQUIRE(updated != nullptr);
    CHECK(updated->launchMode == LaunchMode::Toggle);
    CHECK(updated->launchQuantize == LaunchQuantize::SixteenthBar);
    CHECK(updated->followAction == FollowAction::PlayAgain);
    CHECK(updated->followActionDelayBeats == 2.0);
    CHECK(updated->followActionLoopCount == 3);

    REQUIRE(undo.undo());
    const auto* undone = clips.getClip(clipId);
    REQUIRE(undone != nullptr);
    CHECK(undone->launchMode == LaunchMode::Trigger);
    CHECK(undone->launchQuantize == LaunchQuantize::OneBar);
    CHECK(undone->followAction == FollowAction::None);
    CHECK(undone->followActionDelayBeats == 0.0);
    CHECK(undone->followActionLoopCount == 1);

    REQUIRE(undo.redo());
    CHECK(clips.getClip(clipId)->followAction == FollowAction::PlayAgain);
    undo.clearHistory();
}
