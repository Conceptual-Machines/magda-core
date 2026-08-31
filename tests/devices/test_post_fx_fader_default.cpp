#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/Config.hpp"
#include "magda/daw/core/TrackManager.hpp"

using namespace magda;

namespace {
// Config is a singleton loaded from the user's file, so every test here puts
// back whatever the preference was on the way out.
struct PostFxDefaultFixture {
    PostFxDefaultFixture() : previous_(Config::getInstance().getPostFxPostFaderByDefault()) {
        TrackManager::getInstance().clearAllTracks();
    }
    ~PostFxDefaultFixture() {
        Config::getInstance().setPostFxPostFaderByDefault(previous_);
        TrackManager::getInstance().clearAllTracks();
    }

    bool sideOfNewTrack(bool preferPostFader) {
        Config::getInstance().setPostFxPostFaderByDefault(preferPostFader);
        const auto trackId = TrackManager::getInstance().createTrack("PostFx", TrackType::Media);
        return TrackManager::getInstance().isPostFxPostFader(trackId);
    }

  private:
    bool previous_;
};
}  // namespace

TEST_CASE("A new track takes its post-FX fader side from the preference", "[tracks][postfx]") {
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
