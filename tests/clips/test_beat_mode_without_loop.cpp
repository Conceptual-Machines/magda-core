// Beat mode follows the tempo; whether the clip loops is a separate choice.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/AudioThumbnailManager.hpp"
#include "core/ClipInfo.hpp"
#include "core/ClipManager.hpp"
#include "core/ClipOperations.hpp"
#include "core/SourcePool.hpp"

using Catch::Approx;
using magda::ClipManager;
using magda::ClipOperations;
using magda::PlaybackIntent;
using magda::Provenance;
using magda::SourcePool;

namespace {

constexpr magda::TrackId kTrack = 1;
constexpr double kProjectBpm = 120.0;
constexpr double kSourceBpm = 100.0;
constexpr double kFileSeconds = 8.0;
constexpr const char* kPath = "/tmp/magda-fixtures/beat-no-loop.wav";

struct Fixture {
    Fixture() {
        reset();
        SourcePool::getInstance().seedFactsForTesting(kPath, kFileSeconds, 48000.0);
        SourcePool::getInstance().resolveFacts(SourcePool::getInstance().acquire(kPath));
    }
    ~Fixture() {
        reset();
    }
    static void reset() {
        ClipManager::getInstance().clearAllClips();
        SourcePool::getInstance().clear();
        SourcePool::getInstance().clearSeededFactsForTesting();
        magda::AudioThumbnailManager::getInstance().clearCache();
    }

    /// A 16-beat arrangement clip in beat mode at 100 bpm: 8 s of file is 40/3 beats.
    static magda::ClipId beatClip() {
        auto& clips = ClipManager::getInstance();
        const auto id = clips.createAudioClipBeats(kTrack, 0.0, 16.0, kPath);
        REQUIRE(clips.getClip(id)->primaryEvent()->adoptBpm(kSourceBpm, Provenance::User));
        clips.setPlaybackIntent(id, PlaybackIntent::Beat, kProjectBpm);
        REQUIRE(clips.getClip(id)->primaryEvent()->autoTempo);
        return id;
    }
};

}  // namespace

TEST_CASE("Beat mode leaves a clip that does not loop unlooped", "[clip][beatmode][loop]") {
    Fixture fx;
    const auto id = fx.beatClip();
    REQUIRE_FALSE(ClipManager::getInstance().getClip(id)->loopEnabled);
}

TEST_CASE("Looping can be turned off in beat mode", "[clip][beatmode][loop]") {
    Fixture fx;
    auto& clips = ClipManager::getInstance();
    const auto id = fx.beatClip();
    clips.setClipLoopEnabled(id, true, kProjectBpm);
    REQUIRE(clips.getClip(id)->loopEnabled);

    clips.setClipLoopEnabled(id, false, kProjectBpm);

    const auto* clip = clips.getClip(id);
    REQUIRE_FALSE(clip->loopEnabled);
    REQUIRE(clip->primaryEvent()->autoTempo);
    // The clip ends where the file does, measured at the source's own tempo.
    REQUIRE(clip->placement.lengthBeats == Approx(kFileSeconds * kSourceBpm / 60.0));
}

TEST_CASE("A beat-mode clip that does not loop ends at its source, in beats",
          "[clip][beatmode][loop]") {
    Fixture fx;
    auto& clip = *ClipManager::getInstance().getClip(fx.beatClip());
    clip.primaryEvent()->setAnchorSeconds(2.0);

    ClipOperations::sanitizeAudioToSourceDuration(clip, kFileSeconds, kProjectBpm);

    REQUIRE(clip.placement.lengthBeats == Approx((kFileSeconds - 2.0) * kSourceBpm / 60.0));
}

TEST_CASE("A free clip that does not loop still ends at its source in seconds",
          "[clip][beatmode][loop]") {
    Fixture fx;
    auto& clips = ClipManager::getInstance();
    auto& clip = *clips.getClip(clips.createAudioClipBeats(kTrack, 0.0, 20.0, kPath));
    clip.primaryEvent()->setAnchorSeconds(2.0);

    ClipOperations::sanitizeAudioToSourceDuration(clip, kFileSeconds, kProjectBpm);

    REQUIRE(clip.placement.lengthBeats == Approx((kFileSeconds - 2.0) * kProjectBpm / 60.0));
}

TEST_CASE("Trimming the start of a beat-mode clip that does not loop reads on in source beats",
          "[clip][beatmode][loop]") {
    Fixture fx;
    auto& clips = ClipManager::getInstance();
    const auto id = fx.beatClip();
    clips.resizeClipBeats(id, 15.0, true, kProjectBpm);

    const auto* clip = clips.getClip(id);
    REQUIRE(clip->placement.startBeat == Approx(1.0));
    // One project beat of a 100 bpm source is 0.6 s of file.
    REQUIRE(clip->primaryEvent()->anchorSeconds() == Approx(0.6).margin(1.0e-4));
}
