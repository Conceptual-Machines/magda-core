#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/project/ProjectManager.hpp"

using namespace magda;

namespace {
struct PostFxDefaultFixture {
    PostFxDefaultFixture()
        : previous_(
              ProjectManager::getInstance().getCurrentProjectInfo().defaults.postFxPostFader) {
        TrackManager::getInstance().clearAllTracks();
    }
    ~PostFxDefaultFixture() {
        ProjectManager::getInstance().getMutableProjectInfo().defaults.postFxPostFader = previous_;
        TrackManager::getInstance().clearAllTracks();
    }

    bool sideOfNewTrack(bool preferPostFader) {
        ProjectManager::getInstance().getMutableProjectInfo().defaults.postFxPostFader =
            preferPostFader;
        const auto trackId = TrackManager::getInstance().createTrack("PostFx", TrackType::Media);
        return TrackManager::getInstance().isPostFxPostFader(trackId);
    }

  private:
    bool previous_;
};
}  // namespace

TEST_CASE("A new track takes its post-FX fader side from the project default", "[tracks][postfx]") {
    PostFxDefaultFixture fx;

    SECTION("post-fader preference") {
        REQUIRE(fx.sideOfNewTrack(true));
    }

    SECTION("pre-fader preference") {
        REQUIRE_FALSE(fx.sideOfNewTrack(false));
    }
}

TEST_CASE("The shipped post-FX default is post-fader", "[tracks][postfx]") {
    // The flag's own default, which is what a track deserialized from a project
    // written before the preference existed falls back to.
    REQUIRE(TrackChain{}.postFxPostFader);
}
