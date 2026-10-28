// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

// Beat mode follows the tempo and may play once; only beat mode loops (#3060).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/AudioThumbnailManager.hpp"
#include "core/ClipInfo.hpp"
#include "core/ClipManager.hpp"
#include "core/ClipOperations.hpp"
#include "core/SourcePool.hpp"

using Catch::Approx;
using magda::ClipInfo;
using magda::ClipManager;
using magda::ClipOperations;
using magda::PlaybackIntent;
using magda::Provenance;
using magda::RegionExtent;
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

// =============================================================================
// LOOP on a free clip wraps it at the tempo it is heard at
// =============================================================================

TEST_CASE("LOOP on a free clip with no tempo wraps it at the project tempo",
          "[clip][beatmode][loop]") {
    Fixture fx;
    auto& clips = ClipManager::getInstance();
    const auto id = clips.createAudioClipBeats(kTrack, 0.0, 8.0, kPath);

    clips.setClipLoopEnabled(id, true, kProjectBpm);

    const auto* clip = clips.getClip(id);
    const auto* event = clip->primaryEvent();
    REQUIRE(clip->loopEnabled);
    REQUIRE(event->autoTempo);
    REQUIRE(event->interpBpm == Approx(kProjectBpm));
    REQUIRE(event->bpmFrom == Provenance::Project);
    // It loops what it showed: four seconds of file, and the clip keeps its length.
    REQUIRE(clip->placement.lengthBeats == Approx(8.0));
    REQUIRE(event->loopLengthSeconds() == Approx(4.0).margin(1.0e-4));
}

TEST_CASE("LOOP on a sped-up free clip keeps its speed", "[clip][beatmode][loop]") {
    Fixture fx;
    auto& clips = ClipManager::getInstance();
    const auto id = clips.createAudioClipBeats(kTrack, 0.0, 8.0, kPath);
    clips.getClip(id)->primaryEvent()->speedRatio = 2.0;

    clips.setClipLoopEnabled(id, true, kProjectBpm);

    const auto* event = clips.getClip(id)->primaryEvent();
    REQUIRE(event->autoTempo);
    REQUIRE(event->speedRatio == Approx(1.0));
    // Beat mode reads at projectBpm / interpBpm: twice the file's rate, as before.
    REQUIRE(kProjectBpm / event->interpBpm == Approx(2.0));
}

TEST_CASE("LOOP keeps a tempo the file already carries", "[clip][beatmode][loop]") {
    Fixture fx;
    auto& clips = ClipManager::getInstance();
    const auto id = clips.createAudioClipBeats(kTrack, 0.0, 8.0, kPath);
    REQUIRE(clips.getClip(id)->primaryEvent()->adoptBpm(kSourceBpm, Provenance::Analysis));

    clips.setClipLoopEnabled(id, true, kProjectBpm);

    const auto* event = clips.getClip(id)->primaryEvent();
    REQUIRE(event->autoTempo);
    REQUIRE(event->interpBpm == Approx(kSourceBpm));
    REQUIRE(event->bpmFrom == Provenance::Analysis);
}

TEST_CASE("Leaving beat mode ends the loop", "[clip][beatmode][loop]") {
    Fixture fx;
    auto& clips = ClipManager::getInstance();
    const auto id = fx.beatClip();
    clips.setClipLoopEnabled(id, true, kProjectBpm);

    clips.setPlaybackIntent(id, PlaybackIntent::Free, kProjectBpm);

    const auto* clip = clips.getClip(id);
    REQUIRE_FALSE(clip->primaryEvent()->autoTempo);
    REQUIRE_FALSE(clip->loopEnabled);
}

TEST_CASE("A detected tempo replaces the assumed one and keeps the audio",
          "[clip][beatmode][loop]") {
    Fixture fx;
    auto& clips = ClipManager::getInstance();
    const auto id = clips.createAudioClipBeats(kTrack, 0.0, kFileSeconds * 2.0, kPath);
    clips.setClipLoopEnabled(id, true, kProjectBpm);
    const auto regionBefore = clips.getClip(id)->primaryEvent()->loopLengthSamples;

    clips.adoptAnalysis(id, kPath, kSourceBpm);

    const auto* clip = clips.getClip(id);
    REQUIRE(clip->primaryEvent()->interpBpm == Approx(kSourceBpm));
    REQUIRE(clip->primaryEvent()->bpmFrom == Provenance::Analysis);
    REQUIRE(clip->primaryEvent()->loopLengthSamples == regionBefore);
    // The same file, now heard as 100 bpm, lasts 40/3 project beats.
    REQUIRE(clip->placement.lengthBeats == Approx(kFileSeconds * kSourceBpm / 60.0));
}

// =============================================================================
// A project saved before #3060 can hold free clips that loop
// =============================================================================

namespace {

ClipInfo freeLoop(double speedRatio) {
    auto& clips = ClipManager::getInstance();
    const auto id = clips.createAudioClipBeats(kTrack, 0.0, 16.0, kPath);
    auto clip = *clips.getClip(id);
    clip.loopEnabled = true;
    clip.primaryEvent()->speedRatio = speedRatio;
    return clip;
}

}  // namespace

TEST_CASE("Loading a free loop puts it in beat mode sounding as it did",
          "[clip][beatmode][loop][migration]") {
    Fixture fx;
    std::vector<ClipInfo> staged{freeLoop(1.25)};
    REQUIRE(staged.front().primaryEvent()->adoptBpm(kSourceBpm, Provenance::Analysis));

    ClipOperations::wrapFreeLoops(staged, kProjectBpm);

    const auto& event = *staged.front().primaryEvent();
    REQUIRE(event.autoTempo);
    REQUIRE(staged.front().loopEnabled);
    REQUIRE(event.speedRatio == Approx(1.0));
    REQUIRE(kProjectBpm / event.interpBpm == Approx(1.25));
    REQUIRE(event.bpmFrom == Provenance::Project);
    REQUIRE(event.interpTotalBeats == Approx(kFileSeconds * event.interpBpm / 60.0));
    REQUIRE(staged.front().placement.lengthBeats == Approx(16.0));
}

TEST_CASE("Loading a free loop keeps a tempo the user typed", "[clip][beatmode][loop][migration]") {
    Fixture fx;
    std::vector<ClipInfo> staged{freeLoop(1.25)};
    REQUIRE(staged.front().primaryEvent()->adoptBpm(kSourceBpm, Provenance::User));

    ClipOperations::wrapFreeLoops(staged, kProjectBpm);

    REQUIRE(staged.front().primaryEvent()->autoTempo);
    REQUIRE(staged.front().primaryEvent()->interpBpm == Approx(kSourceBpm));
    REQUIRE(staged.front().primaryEvent()->bpmFrom == Provenance::User);
}

TEST_CASE("Loading leaves clips that do not loop alone", "[clip][beatmode][loop][migration]") {
    Fixture fx;
    auto clip = freeLoop(1.25);
    clip.loopEnabled = false;
    std::vector<ClipInfo> staged{clip};

    ClipOperations::wrapFreeLoops(staged, kProjectBpm);

    REQUIRE_FALSE(staged.front().primaryEvent()->autoTempo);
    REQUIRE(staged.front().primaryEvent()->speedRatio == Approx(1.25));
}
