#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "AudioClipTestHelpers.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/ClipPropertyCommands.hpp"
#include "magda/daw/core/SourcePool.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"

using namespace magda;
using Catch::Approx;

namespace {

struct LabelFixture {
    ClipId id;
    TrackId track;

    LabelFixture() {
        auto& clips = ClipManager::getInstance();
        UndoManager::getInstance().clearHistory();
        clips.clearAllClips();
        TrackManager::getInstance().clearAllTracks();
        track = TrackManager::getInstance().createTrack("Labels");
        id = clips.createAudioClip(track, 0.0, 2.0,
                                   juce::File::getSpecialLocation(juce::File::tempDirectory)
                                       .getChildFile("label-regions-test.wav")
                                       .getFullPathName());
        test::setSourceDuration(*clips.getClip(id), 8.0);
    }
    ~LabelFixture() {
        UndoManager::getInstance().clearHistory();
        ClipManager::getInstance().clearAllClips();
        TrackManager::getInstance().clearAllTracks();
    }

    static const std::vector<LabelRegion>& labelsOf(ClipId clip) {
        return ClipManager::getInstance().getClip(clip)->primaryEvent()->labelRegions;
    }
};

const juce::Colour kRed{0xffcc3333};
const juce::Colour kBlue{0xff3355cc};

}  // namespace

TEST_CASE("Label regions round-trip through the project file", "[clip][labels][serialization]") {
    SourcePool::getInstance().clear();
    ClipInfo original;
    original.id = 3;
    original.trackId = 1;
    auto& event = test::giveAudioEvent(original, "/tmp/labels.wav", 4.0);
    event.labelRegions.push_back({"Verse", kRed, 0.5, 2.25});
    event.labelRegions.push_back({"Hit", kBlue, 3.0, 3.0});

    const auto json = ProjectSerializer::serializeClipInfo(original);
    ClipInfo restored;
    REQUIRE(ProjectSerializer::deserializeClipInfo(json, restored, 120.0));

    const auto& labels = restored.primaryEvent()->labelRegions;
    CHECK(labels == event.labelRegions);
    CHECK(labels[1].isPoint());
    SourcePool::getInstance().clear();
}

TEST_CASE("A clip without labels writes no label key", "[clip][labels][serialization]") {
    SourcePool::getInstance().clear();
    ClipInfo original;
    test::giveAudioEvent(original, "/tmp/nolabels.wav", 4.0);

    const auto json = ProjectSerializer::serializeClipInfo(original);
    const auto audio = json.getDynamicObject()->getProperty("audio");
    const auto& eventJson =
        audio.getDynamicObject()->getProperty("events").getArray()->getReference(0);
    CHECK_FALSE(eventJson.getDynamicObject()->hasProperty("labelRegions"));
    SourcePool::getInstance().clear();
}

TEST_CASE("Setting labels orders them and undoes in one step", "[clip][labels]") {
    LabelFixture f;
    auto& undo = UndoManager::getInstance();

    std::vector<LabelRegion> drawn{{"Late", kRed, 4.0, 5.0}, {"Backwards", kBlue, 2.0, 1.0}};
    undo.executeCommand(std::make_unique<SetClipLabelRegionsCommand>(f.id, drawn));

    const auto& stored = LabelFixture::labelsOf(f.id);
    REQUIRE(stored.size() == 2);
    CHECK(stored[0].name == "Backwards");
    CHECK(stored[0].sourceStart == Approx(1.0));
    CHECK(stored[0].sourceEnd == Approx(2.0));
    CHECK(stored[1].name == "Late");

    undo.undo();
    CHECK(LabelFixture::labelsOf(f.id).empty());
    undo.redo();
    CHECK(LabelFixture::labelsOf(f.id).size() == 2);
}

TEST_CASE("A ghost clip shares its source's labels", "[clip][labels][ghost]") {
    LabelFixture f;
    auto& clips = ClipManager::getInstance();
    clips.setLabelRegions(f.id, {{"Intro", kRed, 0.0, 1.0}});

    const auto ghost = clips.duplicateClipAsGhostAtBeats(f.id, 8.0, f.track);
    REQUIRE(ghost != INVALID_CLIP_ID);
    CHECK(LabelFixture::labelsOf(ghost) == LabelFixture::labelsOf(f.id));

    clips.setLabelRegions(ghost, {{"Renamed", kBlue, 0.0, 1.0}});
    CHECK(LabelFixture::labelsOf(f.id).front().name == "Renamed");
}
