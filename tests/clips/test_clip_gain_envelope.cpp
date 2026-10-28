// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "AudioClipTestHelpers.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/ClipPropertyCommands.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

using namespace magda;
using Catch::Approx;

namespace {

struct EnvelopeFixture {
    ClipId id;
    TrackId track;

    EnvelopeFixture() {
        auto& clips = ClipManager::getInstance();
        UndoManager::getInstance().clearHistory();
        clips.clearAllClips();
        TrackManager::getInstance().clearAllTracks();
        track = TrackManager::getInstance().createTrack("Envelope");
        id = clips.createAudioClip(track, 0.0, 2.0,
                                   juce::File::getSpecialLocation(juce::File::tempDirectory)
                                       .getChildFile("gain-envelope-test.wav")
                                       .getFullPathName());
        test::setSourceDuration(*clips.getClip(id), 8.0);
    }
    ~EnvelopeFixture() {
        UndoManager::getInstance().clearHistory();
        ClipManager::getInstance().clearAllClips();
        TrackManager::getInstance().clearAllTracks();
    }

    static const ClipGainEnvelope& envelopeOf(ClipId clip) {
        return ClipManager::getInstance().getClip(clip)->primaryEvent()->gainEnvelope;
    }
};

ClipGainEnvelope twoPoints(double secondValue) {
    ClipGainEnvelope envelope;
    envelope.points.push_back({.id = 1, .beatPosition = 0.5, .value = 0.75});
    envelope.points.push_back({.id = 2, .beatPosition = 1.5, .value = secondValue});
    return envelope;
}

}  // namespace

TEST_CASE("The gain envelope scale puts unity three quarters up", "[clip][gain-envelope]") {
    using namespace gain_envelope;
    CHECK(unityPosition() == Approx(0.751).margin(1e-3));
    CHECK(positionToGain(unityPosition()) == Approx(1.0));
    CHECK(positionToDb(1.0) == Approx(kMaxDb));
    CHECK(positionToGain(0.0) == 0.0);
    CHECK(dbToPosition(positionToDb(0.3)) == Approx(0.3));
}

TEST_CASE("Envelopes compare by shape, not just point ids", "[clip][gain-envelope]") {
    CHECK(twoPoints(0.2) == twoPoints(0.2));
    CHECK_FALSE(twoPoints(0.2) == twoPoints(0.3));
}

TEST_CASE("Setting a gain envelope tidies it and undoes in one step", "[clip][gain-envelope]") {
    EnvelopeFixture f;
    auto& undo = UndoManager::getInstance();

    ClipGainEnvelope drawn;
    drawn.points.push_back({.id = 4, .beatPosition = 2.0, .value = 1.4});
    drawn.points.push_back({.beatPosition = 0.5, .value = -0.2});
    undo.executeCommand(std::make_unique<SetClipGainEnvelopeCommand>(f.id, drawn));

    const auto& stored = EnvelopeFixture::envelopeOf(f.id);
    REQUIRE(stored.points.size() == 2);
    CHECK(stored.points[0].beatPosition == Approx(0.5));
    CHECK(stored.points[0].value == 0.0);
    CHECK(stored.points[0].id == 5);
    CHECK(stored.points[1].value == 1.0);

    undo.executeCommand(std::make_unique<SetClipGainEnvelopeCommand>(f.id, twoPoints(0.2)));
    undo.executeCommand(std::make_unique<SetClipGainEnvelopeCommand>(f.id, twoPoints(0.4)));
    REQUIRE(undo.undo());
    CHECK(EnvelopeFixture::envelopeOf(f.id) == twoPoints(0.2));
    REQUIRE(undo.undo());
    REQUIRE(undo.undo());
    CHECK(EnvelopeFixture::envelopeOf(f.id).empty());
    REQUIRE(undo.redo());
    CHECK(EnvelopeFixture::envelopeOf(f.id).points.size() == 2);
}

TEST_CASE("A ghost clip shares its sound's gain envelope", "[clip][gain-envelope][ghost]") {
    EnvelopeFixture f;
    auto& clips = ClipManager::getInstance();
    const auto ghost = clips.duplicateClipAsGhostAtBeats(f.id, 8.0, f.track);
    REQUIRE(ghost != INVALID_CLIP_ID);

    clips.setGainEnvelope(f.id, twoPoints(0.3));
    CHECK(EnvelopeFixture::envelopeOf(ghost) == twoPoints(0.3));

    clips.setGainEnvelope(ghost, twoPoints(0.6));
    CHECK(EnvelopeFixture::envelopeOf(f.id) == twoPoints(0.6));
}
