#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>

#include "magda/daw/core/AppPaths.hpp"
#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/Config.hpp"
#include "magda/daw/core/MidiFileWriter.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/AudioEngine.hpp"
#include "magda/daw/media_db/MediaDbContext.hpp"
#include "magda/daw/media_db/MediaDbMetadata.hpp"
#include "magda/daw/project/ProjectManager.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"

using namespace magda;
using Catch::Approx;

namespace {

juce::File testTempRoot() {
    auto envTmp = juce::SystemStats::getEnvironmentVariable("TMPDIR", {});
    auto root = envTmp.isNotEmpty() ? juce::File(envTmp)
                                    : juce::File::getSpecialLocation(juce::File::tempDirectory);
    root.createDirectory();
    return root;
}

juce::File createTestTempFile(const juce::String& suffix) {
    return testTempRoot().getNonexistentChildFile("temp", suffix);
}

void setEnvVar(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void unsetEnvVar(const char* name) {
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

juce::String getEnvVar(const char* name) {
    if (const char* value = std::getenv(name)) {
        return juce::String::fromUTF8(value);
    }
    return {};
}

class ProjectBoundaryResetEngine : public AudioEngine {
  public:
    bool initialize() override {
        return true;
    }

    void shutdown() override {}

    void play() override {
        playing = true;
    }

    void stop() override {
        ++stopCalls;
        playing = false;
        recording = false;
    }

    void pause() override {
        stop();
    }

    void record() override {
        recording = true;
    }

    void locate(double positionSeconds) override {
        ++locateCalls;
        position = positionSeconds;
    }

    double getCurrentPosition() const override {
        return position;
    }

    bool isPlaying() const override {
        return playing;
    }

    bool isRecording() const override {
        return recording;
    }

    double getSessionPlayheadPosition() const override {
        return -1.0;
    }

    ClipId getSessionPlayheadClipId() const override {
        return INVALID_CLIP_ID;
    }

    std::unordered_map<ClipId, double> getActiveClipPlayheadPositions() const override {
        return {};
    }

    SessionClipPlayState getSessionClipPlayState(ClipId) const override {
        return SessionClipPlayState::Stopped;
    }

    void stopSessionTrack(TrackId) override {}

    bool isSessionTrackStopPending(TrackId) const override {
        return false;
    }

    double getAudioThreadTransportSeconds() const override {
        return -1.0;
    }

    void deactivateAllSessionClips() override {
        ++deactivateCalls;
    }

    void setTempo(double bpm) override {
        tempo = bpm;
    }

    double getTempo() const override {
        return tempo;
    }

    void setTimeSignature(int numerator, int denominator) override {
        timeSigNumerator = numerator;
        timeSigDenominator = denominator;
    }

    void setLooping(bool enabled) override {
        ++setLoopingCalls;
        looping = enabled;
    }

    void setLoopRegion(double startSeconds, double endSeconds) override {
        loopStart = startSeconds;
        loopEnd = endSeconds;
    }

    bool isLooping() const override {
        return looping;
    }

    void setMetronomeEnabled(bool enabled) override {
        metronome = enabled;
    }

    bool isMetronomeEnabled() const override {
        return metronome;
    }

    void setCountInMode(int mode) override {
        countInMode = mode;
    }

    int getCountInMode() const override {
        return countInMode;
    }

    void updateTriggerState() override {}
    void processSessionStateEvents() override {}

    juce::AudioDeviceManager* getDeviceManager() override {
        return nullptr;
    }

    AudioBridge* getAudioBridge() override {
        return nullptr;
    }

    const AudioBridge* getAudioBridge() const override {
        return nullptr;
    }

    MidiBridge* getMidiBridge() override {
        return nullptr;
    }

    const MidiBridge* getMidiBridge() const override {
        return nullptr;
    }

    void previewNoteOnTrack(const std::string&, int, int, bool) override {}

    void onTransportPlay(double positionSeconds) override {
        locate(positionSeconds);
        play();
    }

    void onTransportStop(double returnPosition) override {
        stop();
        locate(returnPosition);
    }

    void onTransportPause() override {
        pause();
    }

    void onTransportRecord(double positionSeconds) override {
        locate(positionSeconds);
        record();
    }

    void onTransportStopRecording() override {
        recording = false;
    }

    void onEditPositionChanged(double positionSeconds) override {
        locate(positionSeconds);
    }

    void onTempoChanged(double bpm) override {
        setTempo(bpm);
    }

    void onTimeSignatureChanged(int numerator, int denominator) override {
        setTimeSignature(numerator, denominator);
    }

    void onLoopRegionChanged(double startTime, double endTime, bool enabled) override {
        setLoopRegion(startTime, endTime);
        setLooping(enabled);
    }

    void onLoopEnabledChanged(bool enabled) override {
        setLooping(enabled);
    }

    int stopCalls = 0;
    int deactivateCalls = 0;
    int setLoopingCalls = 0;
    int locateCalls = 0;
    bool playing = true;
    bool recording = true;
    bool looping = true;
    bool metronome = false;
    int countInMode = 0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    double tempo = 120.0;
    double position = 12.0;
    double loopStart = 0.0;
    double loopEnd = 0.0;
};

class ScopedProjectAudioEngine {
  public:
    explicit ScopedProjectAudioEngine(AudioEngine* engine)
        : previousEngine(TrackManager::getInstance().getAudioEngine()) {
        TrackManager::getInstance().setAudioEngine(engine);
    }

    ~ScopedProjectAudioEngine() {
        TrackManager::getInstance().setAudioEngine(previousEngine);
    }

  private:
    AudioEngine* previousEngine = nullptr;
};

}  // namespace

// Test fixture to ensure clean state and temp file cleanup between tests
struct ProjectTestFixture {
    std::vector<juce::File> tempFiles;
    std::vector<juce::File> tempDirs;
    bool previousPersistMixerAnalysis = false;

    ProjectTestFixture()
        : previousPersistMixerAnalysis(Config::getInstance().getPersistMixerAnalysis()) {
        // Clear all singleton state before each test
        TrackManager::getInstance().clearAllTracks();
        ClipManager::getInstance().clearAllClips();
        AutomationManager::getInstance().clearAll();
    }

    ~ProjectTestFixture() {
        // Clean up temp directories (wrapper dirs created by saveProjectAs)
        for (auto& dir : tempDirs) {
            if (dir.isDirectory()) {
                dir.deleteRecursively();
            }
        }

        // Clean up temp files (even if test fails)
        for (auto& file : tempFiles) {
            if (file.existsAsFile()) {
                file.deleteFile();
            }
        }

        // Clean up singleton state after test
        TrackManager::getInstance().clearAllTracks();
        ClipManager::getInstance().clearAllClips();
        AutomationManager::getInstance().clearAll();
        Config::getInstance().setPersistMixerAnalysis(previousPersistMixerAnalysis);
    }

    // Helper to create unique temp file with automatic cleanup
    // suffix: The file extension/suffix to append (e.g., ".mgd")
    juce::File createTempFile(const juce::String& suffix) {
        auto file = createTestTempFile(suffix);
        tempFiles.push_back(file);
        return file;
    }

    // Returns the actual file path after saveProjectAs wraps it in a project directory.
    // e.g., /tmp/foo.mgd -> /tmp/foo/foo.mgd
    static juce::File wrappedPath(const juce::File& file) {
        auto projectName = file.getFileNameWithoutExtension();
        auto parentDir = file.getParentDirectory();
        if (parentDir.getFileName() != projectName) {
            auto wrapperDir = parentDir.getChildFile(projectName);
            return wrapperDir.getChildFile(file.getFileName());
        }
        return file;
    }

    // Create a temp file and register its wrapper directory for cleanup
    juce::File createTempProjectFile(const juce::String& suffix) {
        auto file = createTestTempFile(suffix);
        tempFiles.push_back(file);
        // Register the wrapper directory for cleanup
        auto wrapperDir =
            file.getParentDirectory().getChildFile(file.getFileNameWithoutExtension());
        tempDirs.push_back(wrapperDir);
        return file;
    }
};

struct ScopedTestDataDir {
    juce::String previousDataDir;
    juce::String previousConfigDataDir;
    juce::String previousMediaDbDir;
    juce::File dir;

    explicit ScopedTestDataDir(const juce::String& name)
        : previousDataDir(getEnvVar("MAGDA_DATA_DIR")),
          previousConfigDataDir(magda::Config::getInstance().getDataDir()),
          previousMediaDbDir(magda::Config::getInstance().getMediaDbDir()),
          dir(testTempRoot().getNonexistentChildFile(name, "")) {
        magda::media::MediaDbContext::getInstance().shutdown();
        dir.createDirectory();
        setEnvVar("MAGDA_DATA_DIR", dir.getFullPathName().toRawUTF8());
        magda::Config::getInstance().setDataDir({});
        magda::Config::getInstance().setMediaDbDir({});
        magda::paths::resolve();
    }

    ~ScopedTestDataDir() {
        magda::media::MediaDbContext::getInstance().shutdown();
        if (previousDataDir.isEmpty()) {
            unsetEnvVar("MAGDA_DATA_DIR");
        } else {
            setEnvVar("MAGDA_DATA_DIR", previousDataDir.toRawUTF8());
        }
        magda::Config::getInstance().setDataDir(previousConfigDataDir.toStdString());
        magda::Config::getInstance().setMediaDbDir(previousMediaDbDir.toStdString());
        magda::paths::resolve();
        dir.deleteRecursively();
    }
};

TEST_CASE("Project Serialization Basics", "[project][serialization]") {
    ProjectTestFixture fixture;

    SECTION("Save and load empty project") {
        auto& projectManager = ProjectManager::getInstance();

        // Create unique temp file for testing
        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);

        // Save empty project
        bool saved = projectManager.saveProjectAs(tempFile);
        INFO("saveProjectAs error: " << projectManager.getLastError());
        INFO("tempFile: " << tempFile.getFullPathName());
        REQUIRE(saved == true);
        REQUIRE(actualFile.existsAsFile() == true);

        // Load it back
        bool loaded = projectManager.loadProject(actualFile);
        REQUIRE(loaded == true);

        // Cleanup
    }

    SECTION("Save As serializes migrated media paths") {
        auto& projectManager = ProjectManager::getInstance();
        REQUIRE(projectManager.newProject());

        auto trackId = TrackManager::getInstance().createTrack("Audio Track", TrackType::Audio);

        auto sourceFile =
            projectManager.getRecordingsDirectory().getChildFile("unsaved_recording.wav");
        auto sourceDir = sourceFile.getParentDirectory();
        sourceDir.createDirectory();
        REQUIRE(sourceDir.isDirectory());
        REQUIRE(sourceFile.replaceWithText("placeholder audio"));

        auto clipId = ClipManager::getInstance().createAudioClipBeats(
            trackId, 0.0, 4.0, sourceFile.getFullPathName(), ClipView::Arrangement, 120.0);
        REQUIRE(clipId != INVALID_CLIP_ID);

        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);
        REQUIRE(projectManager.saveProjectAs(tempFile));

        auto expectedFile = actualFile.getParentDirectory()
                                .getChildFile(actualFile.getFileNameWithoutExtension() + "_Media")
                                .getChildFile("recordings")
                                .getChildFile(sourceFile.getFileName());

        StagedProjectData staged;
        REQUIRE(ProjectSerializer::loadAndStage(actualFile, staged));
        REQUIRE(staged.clips.size() == 1);
        REQUIRE(staged.clips[0].isAudio());
        REQUIRE(staged.clips[0].audio().source.filePath == expectedFile.getFullPathName());
        REQUIRE(expectedFile.existsAsFile());
    }

    SECTION("Project info serialization roundtrip") {
        ProjectInfo info;
        info.name = "Test Project";
        info.tempo = 128.0;
        info.timeSignatureNumerator = 3;
        info.timeSignatureDenominator = 4;
        info.loopEnabled = true;
        info.loopStartBeats = 4.0;
        info.loopEndBeats = 16.0;

        // Serialize to JSON
        auto json = ProjectSerializer::serializeProject(info);
        REQUIRE(json.isObject() == true);

        // Deserialize back
        ProjectInfo loaded;
        bool success = ProjectSerializer::deserializeProject(json, loaded);
        REQUIRE(success == true);

        // Verify fields
        REQUIRE(loaded.name == info.name);
        REQUIRE(loaded.tempo == info.tempo);
        REQUIRE(loaded.timeSignatureNumerator == info.timeSignatureNumerator);
        REQUIRE(loaded.timeSignatureDenominator == info.timeSignatureDenominator);
        REQUIRE(loaded.loopEnabled == info.loopEnabled);
        REQUIRE(loaded.loopStartBeats == info.loopStartBeats);
        REQUIRE(loaded.loopEndBeats == info.loopEndBeats);
    }
}

TEST_CASE("Audio clip serialization separates source facts from interpretation",
          "[project][serialization][audio]") {
    ProjectTestFixture fixture;

    auto trackId = TrackManager::getInstance().createTrack("Audio", TrackType::Audio);

    ClipInfo clip;
    clip.id = 42;
    clip.trackId = trackId;
    clip.name = "Loop";
    clip.setAudioContent();
    clip.setPlacementBeats(4.0, 16.0);
    clip.audio().source.filePath = "/tmp/loop.wav";
    clip.audio().source.durationSeconds = 2.7907;
    clip.audio().interpretation.bpm = 172.0;
    clip.audio().interpretation.totalBeats = 8.0;
    clip.audio().interpretation.totalBeatsLocked = true;
    clip.autoTempo = true;
    clip.loopEnabled = true;
    clip.loopStartBeats = 0.0;
    clip.loopLengthBeats = 8.0;
    clip.loopLength = 8.0 * 60.0 / 172.0;
    ClipManager::getInstance().restoreClip(clip);

    ProjectInfo info;
    info.name = "Audio Source Model";
    info.tempo = 120.0;

    auto json = ProjectSerializer::serializeProject(info);
    auto* rootObj = json.getDynamicObject();
    REQUIRE(rootObj != nullptr);

    auto* clips = rootObj->getProperty("clips").getArray();
    REQUIRE(clips != nullptr);
    REQUIRE(clips->size() == 1);

    auto* clipObj = clips->getReference(0).getDynamicObject();
    REQUIRE(clipObj != nullptr);
    REQUIRE(clipObj->getProperty("audioSource").isVoid());

    auto* audioObj = clipObj->getProperty("audio").getDynamicObject();
    REQUIRE(audioObj != nullptr);
    auto* sourceObj = audioObj->getProperty("source").getDynamicObject();
    auto* interpretationObj = audioObj->getProperty("interpretation").getDynamicObject();
    auto* playbackObj = audioObj->getProperty("playback").getDynamicObject();
    REQUIRE(sourceObj != nullptr);
    REQUIRE(interpretationObj != nullptr);
    REQUIRE(playbackObj != nullptr);
    auto* placementObj = clipObj->getProperty("placement").getDynamicObject();
    REQUIRE(placementObj != nullptr);
    REQUIRE(static_cast<double>(sourceObj->getProperty("durationSeconds")) == Approx(2.7907));
    REQUIRE(static_cast<double>(interpretationObj->getProperty("totalBeats")) == Approx(8.0));
    REQUIRE(static_cast<bool>(interpretationObj->getProperty("totalBeatsLocked")));
    REQUIRE(static_cast<double>(playbackObj->getProperty("loopLengthBeats")) == Approx(8.0));
    REQUIRE(static_cast<double>(placementObj->getProperty("lengthBeats")) == Approx(16.0));

    ProjectInfo loaded;
    REQUIRE(ProjectSerializer::deserializeProject(json, loaded));
    auto* restored = ClipManager::getInstance().getClip(clip.id);
    REQUIRE(restored != nullptr);
    REQUIRE(restored->isAudio());
    REQUIRE(restored->placement.lengthBeats == Approx(16.0));
    REQUIRE(restored->audio().source.durationSeconds == Approx(2.7907));
    REQUIRE(restored->audio().interpretation.totalBeats == Approx(8.0));
    REQUIRE(restored->audio().interpretation.totalBeatsLocked);
    REQUIRE(restored->loopLengthBeats == Approx(8.0));
}

TEST_CASE("Session clip follow action settings roundtrip", "[project][serialization][session]") {
    ProjectTestFixture fixture;

    auto trackId = TrackManager::getInstance().createTrack("MIDI", TrackType::Audio);

    ClipInfo clip;
    clip.id = 91;
    clip.trackId = trackId;
    clip.name = "Follow";
    clip.setMidiContent();
    clip.view = ClipView::Session;
    clip.sceneIndex = 2;
    clip.loopEnabled = true;
    clip.setPlacementBeats(0.0, 4.0);
    clip.loopLengthBeats = 4.0;
    clip.launchQuantize = LaunchQuantize::QuarterBar;
    clip.followAction = FollowAction::PlayNext;
    clip.followActionDelayBeats = 0.5;
    clip.followActionLoopCount = 3;
    ClipManager::getInstance().restoreClip(clip);

    ProjectInfo info;
    info.name = "Follow Actions";
    info.tempo = 120.0;

    auto json = ProjectSerializer::serializeProject(info);
    auto* rootObj = json.getDynamicObject();
    REQUIRE(rootObj != nullptr);
    auto* clips = rootObj->getProperty("clips").getArray();
    REQUIRE(clips != nullptr);
    REQUIRE(clips->size() == 1);

    auto* clipObj = clips->getReference(0).getDynamicObject();
    REQUIRE(clipObj != nullptr);
    REQUIRE(static_cast<int>(clipObj->getProperty("followAction")) ==
            static_cast<int>(FollowAction::PlayNext));
    REQUIRE(static_cast<double>(clipObj->getProperty("followActionDelayBeats")) == Approx(0.5));
    REQUIRE(static_cast<int>(clipObj->getProperty("followActionLoopCount")) == 3);

    ProjectInfo loaded;
    REQUIRE(ProjectSerializer::deserializeProject(json, loaded));
    auto* restored = ClipManager::getInstance().getClip(clip.id);
    REQUIRE(restored != nullptr);
    REQUIRE(restored->followAction == FollowAction::PlayNext);
    REQUIRE(restored->followActionDelayBeats == Approx(0.5));
    REQUIRE(restored->followActionLoopCount == 3);
}

TEST_CASE("Looped MIDI clip serialization preserves loop region separate from placement",
          "[project][serialization][midi][loop]") {
    ProjectTestFixture fixture;

    auto trackId = TrackManager::getInstance().createTrack("MIDI", TrackType::Audio);

    ClipInfo clip;
    clip.id = 92;
    clip.trackId = trackId;
    clip.name = "Two Bar Loop";
    clip.setMidiContent();
    clip.view = ClipView::Arrangement;
    clip.midi().sourceFilePath = "/tmp/imported-loop.mid";
    clip.loopEnabled = true;
    clip.setPlacementBeats(0.0, 68.0);  // 17 bars at 4/4
    clip.loopLengthBeats = 8.0;         // 2 bars at 4/4
    clip.loopLength = 4.0;              // 8 beats at 120 BPM
    clip.midiOffset = 1.0;

    MidiNote note;
    note.startBeat = 0.0;
    note.lengthBeats = 1.0;
    note.noteNumber = 60;
    clip.midiNotes.push_back(note);

    ClipManager::getInstance().restoreClip(clip);

    ProjectInfo info;
    info.name = "Looped MIDI";
    info.tempo = 120.0;

    auto json = ProjectSerializer::serializeProject(info);
    auto* rootObj = json.getDynamicObject();
    REQUIRE(rootObj != nullptr);
    auto* clips = rootObj->getProperty("clips").getArray();
    REQUIRE(clips != nullptr);
    REQUIRE(clips->size() == 1);

    auto* clipObj = clips->getReference(0).getDynamicObject();
    REQUIRE(clipObj != nullptr);
    auto* placementObj = clipObj->getProperty("placement").getDynamicObject();
    REQUIRE(placementObj != nullptr);
    REQUIRE(static_cast<double>(placementObj->getProperty("lengthBeats")) == Approx(68.0));
    REQUIRE(static_cast<double>(clipObj->getProperty("loopLengthBeats")) == Approx(8.0));
    auto* midiObj = clipObj->getProperty("midi").getDynamicObject();
    REQUIRE(midiObj != nullptr);
    REQUIRE(midiObj->getProperty("sourceFilePath").toString() == "/tmp/imported-loop.mid");

    ProjectInfo loaded;
    REQUIRE(ProjectSerializer::deserializeProject(json, loaded));
    auto* restored = ClipManager::getInstance().getClip(clip.id);
    REQUIRE(restored != nullptr);
    REQUIRE(restored->isMidi());
    REQUIRE(restored->loopEnabled);
    REQUIRE(restored->placement.lengthBeats == Approx(68.0));
    REQUIRE(restored->loopLengthBeats == Approx(8.0));
    REQUIRE(restored->loopLength == Approx(4.0));
    REQUIRE(restored->midiOffset == Approx(1.0));
    REQUIRE(restored->midi().sourceFilePath == "/tmp/imported-loop.mid");
}

TEST_CASE("Saving a MIDI clip to the media library writes and indexes a generated source file",
          "[project][serialization][midi][media_db]") {
    ProjectTestFixture fixture;
    ScopedTestDataDir dataDir("magda-midi-library-test");

    auto trackId = TrackManager::getInstance().createTrack("MIDI Track", TrackType::Audio);
    auto clipId =
        ClipManager::getInstance().createMidiClipBeats(trackId, 0.0, 4.0, ClipView::Arrangement);
    REQUIRE(clipId != INVALID_CLIP_ID);

    auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip != nullptr);
    clip->name = "Hook MIDI";

    MidiNote note;
    note.startBeat = 0.0;
    note.lengthBeats = 1.0;
    note.noteNumber = 60;
    note.velocity = 100;
    clip->midiNotes.push_back(note);

    MidiCCData cc;
    cc.controller = 1;
    cc.value = 64;
    cc.beatPosition = 0.5;
    clip->midiCCData.push_back(cc);

    REQUIRE(ClipManager::getInstance().canSaveClipToLibrary(clipId));
    REQUIRE(ClipManager::getInstance().saveClipToLibrary(clipId));

    REQUIRE(clip->midi().sourceFilePath.isNotEmpty());
    juce::File savedFile(clip->midi().sourceFilePath);
    REQUIRE(savedFile.existsAsFile());
    REQUIRE(savedFile.hasFileExtension(".mid"));

    juce::File expectedDir(
        juce::String(magda::media::MediaDbContext::getInstance().midiClipsDir().string()));
    REQUIRE(savedFile.getParentDirectory() == expectedDir);
    REQUIRE(magda::media::isFileIndexed(
        std::filesystem::path(clip->midi().sourceFilePath.toStdString())));

    const auto firstPath = clip->midi().sourceFilePath;
    clip->midiNotes.front().velocity = 80;
    REQUIRE(ClipManager::getInstance().saveClipToLibrary(clipId));
    REQUIRE(clip->midi().sourceFilePath == firstPath);
}

TEST_CASE("Saved audio library warp markers survive BPM mismatch re-import",
          "[project][serialization][media_db][warp]") {
    ProjectTestFixture fixture;
    ScopedTestDataDir dataDir("magda-audio-warp-library-test");

    auto sourceFile = dataDir.dir.getChildFile("drum_loop_135bpm.wav");
    REQUIRE(sourceFile.replaceWithText("not decoded in this regression test"));

    auto trackId = TrackManager::getInstance().createTrack("Audio Track", TrackType::Audio);
    auto clipId = ClipManager::getInstance().createAudioClip(
        trackId, 0.0, 4.0, sourceFile.getFullPathName(), ClipView::Arrangement, 120.0);
    REQUIRE(clipId != INVALID_CLIP_ID);

    auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->isAudio());

    clip->autoTempo = true;
    clip->audio().interpretation.bpm = 140.0;
    clip->audio().interpretation.totalBeats = 9.3333333333;
    clip->audio().interpretation.totalBeatsLocked = true;
    clip->warpEnabled = true;
    clip->warpMarkers = {{0.0, 0.0}, {1.234, 1.75}, {3.5, 4.0}};

    REQUIRE(ClipManager::getInstance().saveClipToLibrary(clipId));

    ClipManager::getInstance().clearAllClips();
    auto reimportedId = ClipManager::getInstance().createAudioClip(
        trackId, 0.0, 4.0, sourceFile.getFullPathName(), ClipView::Arrangement, 120.0);
    REQUIRE(reimportedId != INVALID_CLIP_ID);

    auto* reimported = ClipManager::getInstance().getClip(reimportedId);
    REQUIRE(reimported != nullptr);
    REQUIRE(reimported->isAudio());
    REQUIRE(reimported->warpEnabled);
    REQUIRE(reimported->audio().interpretation.bpm == Approx(140.0));
    REQUIRE(reimported->audio().interpretation.totalBeats == Approx(9.3333333333));
    REQUIRE(reimported->warpMarkers.size() == 3);
    REQUIRE(reimported->warpMarkers[1].sourceTime == Approx(1.234));
    REQUIRE(reimported->warpMarkers[1].warpTime == Approx(1.75));
    REQUIRE(reimported->warpMarkers[2].sourceTime == Approx(3.5));
    REQUIRE(reimported->warpMarkers[2].warpTime == Approx(4.0));
}

TEST_CASE("MidiFileWriter writes MIDI clip data to a stable destination",
          "[project][serialization][midi][writer]") {
    ProjectTestFixture fixture;
    auto midiFilePath = fixture.createTempFile(".mid");

    MidiNote note;
    note.startBeat = 0.0;
    note.lengthBeats = 2.0;
    note.noteNumber = 64;
    note.velocity = 96;

    MidiCCData cc;
    cc.controller = 74;
    cc.value = 90;
    cc.beatPosition = 1.0;

    MidiPitchBendData pitchBend;
    pitchBend.value = 9000;
    pitchBend.beatPosition = 1.5;

    std::vector<magda::daw::ChordMarker> markers = {{0.0, 4.0, "Cmaj7"}};

    REQUIRE(magda::daw::MidiFileWriter::writeToFile(midiFilePath, {note}, {cc}, {pitchBend}, 120.0,
                                                    "Writer Clip", markers));
    REQUIRE(midiFilePath.existsAsFile());

    juce::MidiFile midiFile;
    auto stream = midiFilePath.createInputStream();
    REQUIRE(stream != nullptr);
    REQUIRE(midiFile.readFrom(*stream));
    REQUIRE(midiFile.getNumTracks() == 1);

    const auto* track = midiFile.getTrack(0);
    REQUIRE(track != nullptr);

    bool sawTrackName = false;
    bool sawNote = false;
    bool sawCc = false;
    bool sawPitchBend = false;
    bool sawChordMarker = false;
    for (int i = 0; i < track->getNumEvents(); ++i) {
        const auto& msg = track->getEventPointer(i)->message;
        if (msg.isTrackNameEvent() && msg.getTextFromTextMetaEvent() == "Writer Clip")
            sawTrackName = true;
        if (msg.isNoteOn() && msg.getNoteNumber() == 64)
            sawNote = true;
        if (msg.isController() && msg.getControllerNumber() == 74 && msg.getControllerValue() == 90)
            sawCc = true;
        if (msg.isPitchWheel() && msg.getPitchWheelValue() == 9000)
            sawPitchBend = true;
        if (msg.isTextMetaEvent() && msg.getMetaEventType() == 6 &&
            msg.getTextFromTextMetaEvent().startsWith("CHORD:Cmaj7:"))
            sawChordMarker = true;
    }

    REQUIRE(sawTrackName);
    REQUIRE(sawNote);
    REQUIRE(sawCc);
    REQUIRE(sawPitchBend);
    REQUIRE(sawChordMarker);
}

TEST_CASE("Clip serialization validates type and audio schema", "[project][serialization][audio]") {
    ProjectTestFixture fixture;

    auto trackId = TrackManager::getInstance().createTrack("Track", TrackType::Audio);

    ClipInfo clip;
    clip.id = 7;
    clip.trackId = trackId;
    clip.name = "Schema";
    clip.setAudioContent();
    clip.setPlacementBeats(0.0, 16.0);
    clip.audio().source.filePath = "/tmp/schema.wav";
    clip.audio().source.durationSeconds = 4.0;
    clip.audio().interpretation.bpm = 120.0;
    clip.audio().interpretation.totalBeats = 8.0;
    clip.autoTempo = true;
    clip.loopEnabled = true;
    clip.loopLengthBeats = 8.0;
    clip.loopLength = 4.0;
    ClipManager::getInstance().restoreClip(clip);

    ProjectInfo info;
    info.name = "Schema Validation";
    info.tempo = 120.0;

    SECTION("Unknown clip type is rejected") {
        auto json = ProjectSerializer::serializeProject(info);
        auto* rootObj = json.getDynamicObject();
        REQUIRE(rootObj != nullptr);
        auto* projectObj = rootObj->getProperty("project").getDynamicObject();
        REQUIRE(projectObj != nullptr);
        projectObj->setProperty("tempo", 0.0);

        auto* clips = rootObj->getProperty("clips").getArray();
        REQUIRE(clips != nullptr);
        auto* clipObj = clips->getReference(0).getDynamicObject();
        REQUIRE(clipObj != nullptr);

        clipObj->setProperty("type", 999);

        ProjectInfo loaded;
        REQUIRE_FALSE(ProjectSerializer::deserializeProject(json, loaded));
        REQUIRE(ProjectSerializer::getLastError().contains("Unknown clip type"));
    }

    SECTION("MIDI clip with audio payload is rejected") {
        auto json = ProjectSerializer::serializeProject(info);
        auto* rootObj = json.getDynamicObject();
        REQUIRE(rootObj != nullptr);
        auto* clips = rootObj->getProperty("clips").getArray();
        REQUIRE(clips != nullptr);
        auto* clipObj = clips->getReference(0).getDynamicObject();
        REQUIRE(clipObj != nullptr);

        clipObj->setProperty("type", static_cast<int>(ClipType::MIDI));

        ProjectInfo loaded;
        REQUIRE_FALSE(ProjectSerializer::deserializeProject(json, loaded));
        REQUIRE(ProjectSerializer::getLastError().contains("MIDI clip contains audio source data"));
    }

    SECTION("Legacy audioSource payload migrates to the audio model") {
        auto json = ProjectSerializer::serializeProject(info);
        auto* rootObj = json.getDynamicObject();
        REQUIRE(rootObj != nullptr);
        auto* clips = rootObj->getProperty("clips").getArray();
        REQUIRE(clips != nullptr);
        auto* clipObj = clips->getReference(0).getDynamicObject();
        REQUIRE(clipObj != nullptr);

        clipObj->removeProperty("audio");
        auto* sourceObj = new juce::DynamicObject();
        sourceObj->setProperty("filePath", "/tmp/legacy.wav");
        sourceObj->setProperty("offsetSeconds", 0.5);
        sourceObj->setProperty("offsetBeats", 1.0);
        sourceObj->setProperty("loopStartSeconds", 0.25);
        sourceObj->setProperty("loopLengthSeconds", 4.0);
        sourceObj->setProperty("loopStartBeats", 0.5);
        sourceObj->setProperty("loopLengthBeats", 8.0);
        sourceObj->setProperty("speedRatio", 1.25);
        sourceObj->setProperty("sourceNumBeats", 8.0);
        sourceObj->setProperty("sourceBPM", 120.0);
        sourceObj->setProperty("warpEnabled", true);
        auto* warpObj = new juce::DynamicObject();
        warpObj->setProperty("sourceTime", 1.0);
        warpObj->setProperty("warpTime", 1.25);
        juce::Array<juce::var> warpMarkers;
        warpMarkers.add(juce::var(warpObj));
        sourceObj->setProperty("warpMarkers", warpMarkers);
        clipObj->setProperty("audioSource", juce::var(sourceObj));

        ClipManager::getInstance().clearAllClips();

        ProjectInfo loaded;
        REQUIRE(ProjectSerializer::deserializeProject(json, loaded));
        auto* restored = ClipManager::getInstance().getClip(clip.id);
        REQUIRE(restored != nullptr);
        REQUIRE(restored->isAudio());
        REQUIRE(restored->audio().source.filePath == "/tmp/legacy.wav");
        REQUIRE(restored->audio().source.durationSeconds == Approx(4.0));
        REQUIRE(restored->audio().interpretation.totalBeats == Approx(8.0));
        REQUIRE(restored->audio().interpretation.bpm == Approx(120.0));
        REQUIRE(restored->offset == Approx(0.5));
        REQUIRE(restored->offsetBeats == Approx(1.0));
        REQUIRE(restored->loopStart == Approx(0.25));
        REQUIRE(restored->loopLength == Approx(4.0));
        REQUIRE(restored->loopStartBeats == Approx(0.5));
        REQUIRE(restored->loopLengthBeats == Approx(8.0));
        REQUIRE(restored->speedRatio == Approx(1.25));
        REQUIRE(restored->warpEnabled);
        REQUIRE(restored->warpMarkers.size() == 1);
        REQUIRE(restored->warpMarkers.front().sourceTime == Approx(1.0));
        REQUIRE(restored->warpMarkers.front().warpTime == Approx(1.25));
    }

    SECTION("Legacy flat audio clip payload migrates placement and audio model") {
        auto json = ProjectSerializer::serializeProject(info);
        auto* rootObj = json.getDynamicObject();
        REQUIRE(rootObj != nullptr);
        auto* clips = rootObj->getProperty("clips").getArray();
        REQUIRE(clips != nullptr);
        auto* clipObj = clips->getReference(0).getDynamicObject();
        REQUIRE(clipObj != nullptr);

        clipObj->removeProperty("placement");
        clipObj->removeProperty("audio");
        clipObj->setProperty("startTime", 2.0);
        clipObj->setProperty("length", 6.0);
        clipObj->removeProperty("startBeats");
        clipObj->removeProperty("lengthBeats");
        clipObj->setProperty("audioFilePath", "/tmp/flat-legacy.wav");
        clipObj->setProperty("offset", 0.25);
        clipObj->setProperty("offsetBeats", 0.5);
        clipObj->setProperty("loopStart", 0.125);
        clipObj->setProperty("loopLength", 6.0);
        clipObj->setProperty("loopStartBeats", 0.25);
        clipObj->setProperty("loopLengthBeats", 12.0);
        clipObj->setProperty("speedRatio", 1.5);
        clipObj->setProperty("sourceNumBeats", 12.0);
        clipObj->setProperty("sourceBPM", 120.0);

        ClipManager::getInstance().clearAllClips();

        ProjectInfo loaded;
        REQUIRE(ProjectSerializer::deserializeProject(json, loaded));
        auto* restored = ClipManager::getInstance().getClip(clip.id);
        REQUIRE(restored != nullptr);
        REQUIRE(restored->isAudio());
        REQUIRE(restored->placement.startBeat == Approx(4.0));
        REQUIRE(restored->placement.lengthBeats == Approx(12.0));
        REQUIRE(restored->length == Approx(6.0));
        REQUIRE(restored->audio().source.filePath == "/tmp/flat-legacy.wav");
        REQUIRE(restored->audio().source.durationSeconds == Approx(6.0));
        REQUIRE(restored->audio().interpretation.totalBeats == Approx(12.0));
        REQUIRE(restored->audio().interpretation.bpm == Approx(120.0));
        REQUIRE(restored->offset == Approx(0.25));
        REQUIRE(restored->offsetBeats == Approx(0.5));
        REQUIRE(restored->loopStart == Approx(0.125));
        REQUIRE(restored->loopLength == Approx(6.0));
        REQUIRE(restored->loopStartBeats == Approx(0.25));
        REQUIRE(restored->loopLengthBeats == Approx(12.0));
        REQUIRE(restored->speedRatio == Approx(1.5));
    }
}

TEST_CASE("Automation serialization uses beat-domain property names",
          "[project][serialization][automation][beats]") {
    ProjectTestFixture fixture;

    auto trackId = TrackManager::getInstance().createTrack("Automation", TrackType::Audio);
    auto& automation = AutomationManager::getInstance();
    auto laneId =
        automation.createLane(ControlTarget::trackVolume(trackId), AutomationLaneType::ClipBased);
    automation.setLaneSnapEditsToBeatGrid(laneId, false);
    auto clipId = automation.createClip(laneId, 4.0, 8.0);
    REQUIRE(clipId != INVALID_AUTOMATION_CLIP_ID);
    automation.setClipLooping(clipId, true);
    automation.setClipLoopLength(clipId, 2.0);
    auto pointId = automation.addPointToClip(clipId, 1.5, 0.75, AutomationCurveType::Bezier);
    REQUIRE(pointId != INVALID_AUTOMATION_POINT_ID);
    BezierHandle inHandle;
    inHandle.beatOffset = -0.25;
    inHandle.value = -0.1;
    BezierHandle outHandle;
    outHandle.beatOffset = 0.5;
    outHandle.value = 0.2;
    automation.setPointHandlesInClip(clipId, pointId, inHandle, outHandle);

    ProjectInfo info;
    info.name = "Automation Beats";
    auto json = ProjectSerializer::serializeProject(info);
    auto* rootObj = json.getDynamicObject();
    REQUIRE(rootObj != nullptr);
    auto* automationObj = rootObj->getProperty("automation").getDynamicObject();
    REQUIRE(automationObj != nullptr);
    auto* lanes = automationObj->getProperty("lanes").getArray();
    REQUIRE(lanes != nullptr);
    REQUIRE(lanes->size() == 1);
    auto* laneObj = lanes->getReference(0).getDynamicObject();
    REQUIRE(laneObj != nullptr);
    REQUIRE(laneObj->hasProperty("snapEditsToBeatGrid"));
    REQUIRE_FALSE(laneObj->hasProperty("snapToBeatGrid"));
    REQUIRE_FALSE(laneObj->hasProperty("snapTime"));
    REQUIRE(static_cast<bool>(laneObj->getProperty("snapEditsToBeatGrid")) == false);

    auto* clips = automationObj->getProperty("clips").getArray();
    REQUIRE(clips != nullptr);
    REQUIRE(clips->size() == 1);

    auto* obj = clips->getReference(0).getDynamicObject();
    REQUIRE(obj != nullptr);
    REQUIRE(obj->hasProperty("startBeats"));
    REQUIRE(obj->hasProperty("lengthBeats"));
    REQUIRE(obj->hasProperty("loopLengthBeats"));
    REQUIRE_FALSE(obj->hasProperty("startTime"));
    REQUIRE_FALSE(obj->hasProperty("length"));
    REQUIRE_FALSE(obj->hasProperty("loopLength"));
    REQUIRE(static_cast<double>(obj->getProperty("startBeats")) == Approx(4.0));
    REQUIRE(static_cast<double>(obj->getProperty("lengthBeats")) == Approx(8.0));
    REQUIRE(static_cast<double>(obj->getProperty("loopLengthBeats")) == Approx(2.0));

    auto* points = obj->getProperty("points").getArray();
    REQUIRE(points != nullptr);
    REQUIRE(points->size() == 1);
    auto* pointObj = points->getReference(0).getDynamicObject();
    REQUIRE(pointObj != nullptr);
    REQUIRE(pointObj->hasProperty("beatPosition"));
    REQUIRE_FALSE(pointObj->hasProperty("time"));
    REQUIRE(static_cast<double>(pointObj->getProperty("beatPosition")) == Approx(1.5));

    auto* inHandleObj = pointObj->getProperty("inHandle").getDynamicObject();
    auto* outHandleObj = pointObj->getProperty("outHandle").getDynamicObject();
    REQUIRE(inHandleObj != nullptr);
    REQUIRE(outHandleObj != nullptr);
    REQUIRE(inHandleObj->hasProperty("beatOffset"));
    REQUIRE(outHandleObj->hasProperty("beatOffset"));
    REQUIRE_FALSE(inHandleObj->hasProperty("time"));
    REQUIRE_FALSE(outHandleObj->hasProperty("time"));
    REQUIRE(static_cast<double>(inHandleObj->getProperty("beatOffset")) == Approx(-0.25));
    REQUIRE(static_cast<double>(outHandleObj->getProperty("beatOffset")) == Approx(0.5));

    REQUIRE(ProjectSerializer::deserializeProject(json, info));
    const auto& restoredClips = AutomationManager::getInstance().getClips();
    REQUIRE(restoredClips.size() == 1);
    REQUIRE(restoredClips[0].startBeats == Approx(4.0));
    REQUIRE(restoredClips[0].lengthBeats == Approx(8.0));
    REQUIRE(restoredClips[0].loopLengthBeats == Approx(2.0));
    REQUIRE(restoredClips[0].points.size() == 1);
    REQUIRE(restoredClips[0].points[0].beatPosition == Approx(1.5));
    REQUIRE(restoredClips[0].points[0].inHandle.beatOffset == Approx(-0.25));
    REQUIRE(restoredClips[0].points[0].outHandle.beatOffset == Approx(0.5));
    const auto* restoredLane = AutomationManager::getInstance().getLane(laneId);
    REQUIRE(restoredLane != nullptr);
    REQUIRE_FALSE(restoredLane->snapEditsToBeatGrid);
}

TEST_CASE("Automation serialization reads legacy time-named beat properties",
          "[project][serialization][automation][beats]") {
    ProjectTestFixture fixture;

    auto* obj = new juce::DynamicObject();
    obj->setProperty("id", 9);
    obj->setProperty("laneId", 4);
    obj->setProperty("name", "Legacy Auto");
    obj->setProperty("colour", "#ff00ff00");
    obj->setProperty("startTime", 3.0);
    obj->setProperty("length", 6.0);
    obj->setProperty("looping", true);
    obj->setProperty("loopLength", 1.5);

    auto* pointObj = new juce::DynamicObject();
    pointObj->setProperty("id", 13);
    pointObj->setProperty("time", 2.25);
    pointObj->setProperty("value", 0.4);
    pointObj->setProperty("curveType", static_cast<int>(AutomationCurveType::Bezier));
    pointObj->setProperty("tension", 0.1);

    auto* inHandleObj = new juce::DynamicObject();
    inHandleObj->setProperty("time", -0.5);
    inHandleObj->setProperty("value", -0.2);
    inHandleObj->setProperty("linked", false);
    pointObj->setProperty("inHandle", juce::var(inHandleObj));

    auto* outHandleObj = new juce::DynamicObject();
    outHandleObj->setProperty("time", 0.75);
    outHandleObj->setProperty("value", 0.3);
    outHandleObj->setProperty("linked", false);
    pointObj->setProperty("outHandle", juce::var(outHandleObj));

    juce::Array<juce::var> points;
    points.add(juce::var(pointObj));
    obj->setProperty("points", juce::var(points));

    juce::Array<juce::var> clips;
    clips.add(juce::var(obj));

    auto* automationObj = new juce::DynamicObject();
    automationObj->setProperty("lanes", juce::Array<juce::var>{});
    automationObj->setProperty("clips", juce::var(clips));

    ProjectInfo info;
    auto json = ProjectSerializer::serializeProject(info);
    auto* rootObj = json.getDynamicObject();
    REQUIRE(rootObj != nullptr);
    rootObj->setProperty("automation", juce::var(automationObj));

    REQUIRE(ProjectSerializer::deserializeProject(json, info));
    const auto& restoredClips = AutomationManager::getInstance().getClips();
    REQUIRE(restoredClips.size() == 1);
    REQUIRE(restoredClips[0].startBeats == Approx(3.0));
    REQUIRE(restoredClips[0].lengthBeats == Approx(6.0));
    REQUIRE(restoredClips[0].looping);
    REQUIRE(restoredClips[0].loopLengthBeats == Approx(1.5));
    REQUIRE(restoredClips[0].points.size() == 1);
    REQUIRE(restoredClips[0].points[0].beatPosition == Approx(2.25));
    REQUIRE(restoredClips[0].points[0].inHandle.beatOffset == Approx(-0.5));
    REQUIRE(restoredClips[0].points[0].outHandle.beatOffset == Approx(0.75));
}

TEST_CASE("Project with Tracks", "[project][serialization][tracks]") {
    ProjectTestFixture fixture;

    SECTION("Save and load project with tracks") {
        auto& trackManager = TrackManager::getInstance();
        auto& projectManager = ProjectManager::getInstance();

        // Create a couple tracks
        trackManager.createTrack("Audio 1", TrackType::Audio);
        trackManager.createTrack("MIDI 1", TrackType::Audio);

        REQUIRE(trackManager.getTracks().size() == 2);

        // Create unique temp file
        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);

        // Save
        bool saved = projectManager.saveProjectAs(tempFile);
        REQUIRE(saved == true);

        // Clear tracks
        trackManager.clearAllTracks();
        REQUIRE(trackManager.getTracks().size() == 0);

        // Load back
        bool loaded = projectManager.loadProject(actualFile);
        REQUIRE(loaded == true);

        // Verify tracks restored
        const auto& tracks = trackManager.getTracks();
        REQUIRE(tracks.size() == 2);
        REQUIRE(tracks[0].name == "Audio 1");
        REQUIRE(tracks[0].type == TrackType::Audio);
        REQUIRE(tracks[1].name == "MIDI 1");
        REQUIRE(tracks[1].type == TrackType::Audio);

        // Cleanup
        trackManager.clearAllTracks();
    }
}

TEST_CASE("Project File Format", "[project][serialization][file]") {
    ProjectTestFixture fixture;

    SECTION("File has .mgd extension") {
        auto& projectManager = ProjectManager::getInstance();

        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);

        bool saved = projectManager.saveProjectAs(tempFile);
        REQUIRE(saved == true);
        REQUIRE(actualFile.hasFileExtension(".mgd") == true);
    }

    SECTION("File is not empty") {
        auto& projectManager = ProjectManager::getInstance();

        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);

        bool saved = projectManager.saveProjectAs(tempFile);
        REQUIRE(saved == true);
        REQUIRE(actualFile.getSize() > 0);
    }
}

TEST_CASE("Project Manager State", "[project][manager]") {
    ProjectTestFixture fixture;

    SECTION("hasUnsavedChanges tracks dirty state") {
        auto& projectManager = ProjectManager::getInstance();
        auto& trackManager = TrackManager::getInstance();

        // Create new project (should be clean)
        projectManager.newProject();
        REQUIRE(projectManager.hasUnsavedChanges() == false);

        // Make a change
        trackManager.createTrack("Test", TrackType::Audio);
        projectManager.markDirty();

        REQUIRE(projectManager.hasUnsavedChanges() == true);

        // Save should clear dirty flag
        auto tempFile = fixture.createTempProjectFile(".mgd");

        REQUIRE(projectManager.saveProjectAs(tempFile) == true);
        REQUIRE(projectManager.hasUnsavedChanges() == false);

        // Cleanup
        trackManager.clearAllTracks();
    }

    SECTION("getCurrentProjectFile returns correct file") {
        auto& projectManager = ProjectManager::getInstance();

        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);
        REQUIRE(projectManager.saveProjectAs(tempFile) == true);

        auto currentFile = projectManager.getCurrentProjectFile();
        REQUIRE(currentFile.getFullPathName() == actualFile.getFullPathName());
    }

    SECTION("hasOpenProject tracks project lifecycle correctly") {
        auto& projectManager = ProjectManager::getInstance();

        // Create new project - should be open even though clean and unsaved
        projectManager.newProject();
        REQUIRE(projectManager.hasOpenProject() == true);
        REQUIRE(projectManager.hasUnsavedChanges() == false);

        // Save project - should still be open
        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);
        REQUIRE(projectManager.saveProjectAs(tempFile) == true);
        REQUIRE(projectManager.hasOpenProject() == true);

        // Close project - should not be open
        projectManager.closeProject();
        REQUIRE(projectManager.hasOpenProject() == false);

        // Load project - should be open again
        projectManager.loadProject(actualFile);
        REQUIRE(projectManager.hasOpenProject() == true);

        // Close again
        projectManager.closeProject();
        REQUIRE(projectManager.hasOpenProject() == false);

        // Cleanup
    }

    SECTION("project boundaries reset transport and session state") {
        auto& projectManager = ProjectManager::getInstance();
        ProjectBoundaryResetEngine engine;
        ScopedProjectAudioEngine scopedEngine(&engine);

        REQUIRE(projectManager.newProject() == true);

        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);
        REQUIRE(projectManager.saveProjectAs(tempFile) == true);

        REQUIRE(projectManager.loadProject(actualFile) == true);
        REQUIRE(projectManager.closeProject() == true);

        REQUIRE(engine.stopCalls == 3);
        REQUIRE(engine.deactivateCalls == 3);
        REQUIRE(engine.setLoopingCalls == 3);
        REQUIRE(engine.locateCalls == 3);
        REQUIRE(engine.playing == false);
        REQUIRE(engine.recording == false);
        REQUIRE(engine.looping == false);
        REQUIRE(engine.position == Approx(0.0));
    }
}

TEST_CASE("Error Handling", "[project][serialization][errors]") {
    ProjectTestFixture fixture;

    SECTION("Load non-existent file fails gracefully") {
        auto& projectManager = ProjectManager::getInstance();

        auto nonExistentFile =
            testTempRoot().getNonexistentChildFile("this_does_not_exist", ".mgd");

        bool loaded = projectManager.loadProject(nonExistentFile);
        REQUIRE(loaded == false);
        REQUIRE(projectManager.getLastError().isNotEmpty() == true);
    }

    SECTION("Save to invalid path fails gracefully") {
        auto& projectManager = ProjectManager::getInstance();

        // Use a path inside a regular file (not a directory) so directory
        // creation fails — you can't create a subdirectory inside a file.
        auto blockingFile = testTempRoot().getChildFile("blocking_file_for_project_test");
        blockingFile.create();
        auto invalidFile = blockingFile.getChildFile("sub").getChildFile("test.mgd");

        bool saved = projectManager.saveProjectAs(invalidFile);
        REQUIRE(saved == false);

        // Cleanup
        blockingFile.deleteFile();
    }
}

TEST_CASE("Comprehensive Project Serialization", "[project][serialization][comprehensive]") {
    ProjectTestFixture fixture;

    SECTION("Save and load project with clips and devices") {
        auto& projectManager = ProjectManager::getInstance();
        auto& trackManager = TrackManager::getInstance();
        auto& clipManager = ClipManager::getInstance();

        // Create a track
        auto trackId = trackManager.createTrack("Test MIDI Track", TrackType::Audio);
        auto* track = trackManager.getTrack(trackId);
        REQUIRE(track != nullptr);

        // Add a device to the track
        DeviceInfo device;
        device.id = 1;
        device.name = "Test Synth";
        device.pluginId = "TestSynth";
        device.manufacturer = "Test";
        device.format = PluginFormat::VST3;
        device.isInstrument = true;
        device.bypassed = false;
        trackManager.addDeviceToTrack(trackId, device);

        // Add a MIDI clip to the track
        auto clipId = clipManager.createMidiClip(trackId, 0.0, 4.0);

        // Get the clip and add some MIDI notes directly
        auto* clip = clipManager.getClip(clipId);
        REQUIRE(clip != nullptr);

        MidiNote note1;
        note1.noteNumber = 60;
        note1.velocity = 100;
        note1.startBeat = 0.0;
        note1.lengthBeats = 1.0;
        clip->midiNotes.push_back(note1);

        MidiNote note2;
        note2.noteNumber = 64;
        note2.velocity = 80;
        note2.startBeat = 1.0;
        note2.lengthBeats = 1.0;
        clip->midiNotes.push_back(note2);

        // Save the project
        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);
        bool saved = projectManager.saveProjectAs(tempFile);
        REQUIRE(saved == true);

        // Clear everything
        trackManager.clearAllTracks();
        clipManager.clearAllClips();

        // Verify cleared
        REQUIRE(trackManager.getTracks().empty() == true);
        REQUIRE(clipManager.getClips().empty() == true);

        // Load the project back
        bool loaded = projectManager.loadProject(actualFile);
        REQUIRE(loaded == true);

        // Verify the track was restored
        const auto& tracks = trackManager.getTracks();
        REQUIRE(tracks.size() == 1);
        REQUIRE(tracks[0].type == TrackType::Audio);

        // Verify the device was restored
        REQUIRE(tracks[0].chain.fxChainElements.size() == 1);
        REQUIRE(isDevice(tracks[0].chain.fxChainElements[0]) == true);
        const auto& restoredDevice = getDevice(tracks[0].chain.fxChainElements[0]);
        REQUIRE(restoredDevice.name == "Test Synth");
        REQUIRE(restoredDevice.isInstrument == true);

        // Verify the clip was restored
        const auto& clips = clipManager.getClips();
        REQUIRE(clips.size() == 1);
        REQUIRE(clips[0].name == "MIDI 1");  // Default name from createMidiClip
        REQUIRE(clips[0].getType() == ClipType::MIDI);
        REQUIRE(clips[0].midiNotes.size() == 2);
        REQUIRE(clips[0].midiNotes[0].noteNumber == 60);
        REQUIRE(clips[0].midiNotes[1].noteNumber == 64);

        // Cleanup
    }

    SECTION("Save and load project with rack") {
        auto& projectManager = ProjectManager::getInstance();
        auto& trackManager = TrackManager::getInstance();

        // Create a track
        auto trackId = trackManager.createTrack("Test Audio Track", TrackType::Audio);

        // Add a rack to the track
        auto rackId = trackManager.addRackToTrack(trackId, "Test Rack");
        REQUIRE(rackId != INVALID_RACK_ID);

        // Save the project
        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);
        bool saved = projectManager.saveProjectAs(tempFile);
        REQUIRE(saved == true);

        // Clear everything
        trackManager.clearAllTracks();

        // Load the project back
        bool loaded = projectManager.loadProject(actualFile);
        REQUIRE(loaded == true);

        // Verify the track was restored
        const auto& tracks = trackManager.getTracks();
        REQUIRE(tracks.size() == 1);

        // Verify the rack was restored
        REQUIRE(tracks[0].chain.fxChainElements.size() == 1);
        REQUIRE(isRack(tracks[0].chain.fxChainElements[0]) == true);
        const auto& restoredRack = getRack(tracks[0].chain.fxChainElements[0]);
        REQUIRE(restoredRack.name == "Test Rack");

        // Cleanup
    }
}

TEST_CASE("Project metadata fields roundtrip", "[project][serialization][metadata]") {
    ProjectTestFixture fixture;

    SECTION("sampleRate, keyRoot, keyQuality serialize and deserialize") {
        ProjectInfo info;
        info.name = "Metadata Test";
        info.tempo = 140.0;
        info.sampleRate = 96000.0;
        info.keyRoot = 7;     // G
        info.keyQuality = 1;  // minor

        auto json = ProjectSerializer::serializeProject(info);
        REQUIRE(json.isObject() == true);

        ProjectInfo loaded;
        bool success = ProjectSerializer::deserializeProject(json, loaded);
        REQUIRE(success == true);

        REQUIRE(loaded.sampleRate == 96000.0);
        REQUIRE(loaded.keyRoot == 7);
        REQUIRE(loaded.keyQuality == 1);
    }

    SECTION("Missing metadata fields use defaults (backward compat)") {
        // Simulate an old project JSON without the new fields
        ProjectInfo info;
        info.name = "Old Project";
        info.tempo = 120.0;
        // Don't set sampleRate, keyRoot, keyQuality — use defaults

        auto json = ProjectSerializer::serializeProject(info);

        // Manually strip the new fields from the project object
        auto* rootObj = json.getDynamicObject();
        auto projectVar = rootObj->getProperty("project");
        auto* projectObj = projectVar.getDynamicObject();
        projectObj->removeProperty("sampleRate");
        projectObj->removeProperty("keyRoot");
        projectObj->removeProperty("keyQuality");

        ProjectInfo loaded;
        bool success = ProjectSerializer::deserializeProject(json, loaded);
        REQUIRE(success == true);

        // Should fall back to defaults
        REQUIRE(loaded.sampleRate == 44100.0);
        REQUIRE(loaded.keyRoot == -1);
        REQUIRE(loaded.keyQuality == 0);
    }
}

TEST_CASE("DeviceInfo pluginState roundtrip", "[project][serialization][pluginState]") {
    ProjectTestFixture fixture;

    SECTION("pluginState is serialized and deserialized") {
        auto& trackManager = TrackManager::getInstance();

        auto trackId = trackManager.createTrack("Plugin State Track", TrackType::Audio);

        DeviceInfo device;
        device.id = 1;
        device.name = "Test Plugin";
        device.pluginId = "TestPlugin";
        device.manufacturer = "TestCo";
        device.format = PluginFormat::VST3;
        device.isInstrument = true;
        device.pluginState = "SGVsbG8gV29ybGQ=";  // base64 for "Hello World"
        trackManager.addDeviceToTrack(trackId, device);

        auto& projectManager = ProjectManager::getInstance();
        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);
        bool saved = projectManager.saveProjectAs(tempFile);
        REQUIRE(saved == true);

        trackManager.clearAllTracks();

        bool loaded = projectManager.loadProject(actualFile);
        REQUIRE(loaded == true);

        const auto& tracks = trackManager.getTracks();
        REQUIRE(tracks.size() == 1);
        REQUIRE(tracks[0].chain.fxChainElements.size() == 1);
        REQUIRE(isDevice(tracks[0].chain.fxChainElements[0]) == true);

        const auto& restoredDevice = getDevice(tracks[0].chain.fxChainElements[0]);
        REQUIRE(restoredDevice.pluginState == juce::String("SGVsbG8gV29ybGQ="));
    }

    SECTION("Device without pluginState roundtrips with empty state") {
        auto& trackManager = TrackManager::getInstance();
        auto& projectManager = ProjectManager::getInstance();

        auto trackId = trackManager.createTrack("No State Track", TrackType::Audio);

        DeviceInfo device;
        device.id = 1;
        device.name = "No State Plugin";
        device.pluginId = "NoState";
        device.format = PluginFormat::Internal;
        // pluginState is empty by default
        trackManager.addDeviceToTrack(trackId, device);

        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);
        REQUIRE(projectManager.saveProjectAs(tempFile) == true);

        trackManager.clearAllTracks();
        REQUIRE(projectManager.loadProject(actualFile) == true);

        const auto& tracks = trackManager.getTracks();
        REQUIRE(tracks.size() == 1);
        REQUIRE(tracks[0].chain.fxChainElements.size() == 1);
        REQUIRE(isDevice(tracks[0].chain.fxChainElements[0]) == true);
        const auto& restoredDevice = getDevice(tracks[0].chain.fxChainElements[0]);
        REQUIRE(restoredDevice.pluginState.isEmpty());
    }
}

TEST_CASE("TrackInfo persistent runtime seeds roundtrip", "[project][serialization][track]") {
    ProjectTestFixture fixture;

    auto& trackManager = TrackManager::getInstance();
    auto trackId = trackManager.createTrack("Session Track", TrackType::Audio);
    auto* track = trackManager.getTrack(trackId);
    REQUIRE(track != nullptr);
    track->volume = 0.25f;
    track->pan = -0.5f;
    track->manualVolume = 0.75f;
    track->manualPan = 0.25f;
    track->playbackMode = TrackPlaybackMode::Session;

    ProjectInfo info;
    auto json = ProjectSerializer::serializeProject(info);

    auto* rootObj = json.getDynamicObject();
    REQUIRE(rootObj != nullptr);
    auto* tracks = rootObj->getProperty("tracks").getArray();
    REQUIRE(tracks != nullptr);
    REQUIRE(tracks->size() == 1);
    auto* trackObj = tracks->getReference(0).getDynamicObject();
    REQUIRE(trackObj != nullptr);
    REQUIRE(trackObj->hasProperty("manualVolume"));
    REQUIRE(trackObj->hasProperty("manualPan"));
    REQUIRE(trackObj->hasProperty("playbackMode"));

    ProjectInfo loadedInfo;
    REQUIRE(ProjectSerializer::deserializeProject(json, loadedInfo));
    auto* loaded = trackManager.getTrack(trackId);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->volume == Approx(0.25f));
    REQUIRE(loaded->pan == Approx(-0.5f));
    REQUIRE(loaded->manualVolume == Approx(0.75f));
    REQUIRE(loaded->manualPan == Approx(0.25f));
    REQUIRE(loaded->playbackMode == TrackPlaybackMode::Session);
}

TEST_CASE("DeviceInfo panel UI state roundtrip", "[project][serialization][device]") {
    ProjectTestFixture fixture;

    auto& trackManager = TrackManager::getInstance();
    auto trackId = trackManager.createTrack("Device Track", TrackType::Audio);

    DeviceInfo device;
    device.id = 23;
    device.name = "AI Device";
    device.pluginId = "internal.ai";
    device.format = PluginFormat::Internal;
    device.modPanelOpen = true;
    device.gainPanelOpen = true;
    device.paramPanelOpen = true;
    device.aiPanelOpen = true;
    device.aiPanelOutput = "transient output";
    REQUIRE(trackManager.addDeviceToTrack(trackId, device) != INVALID_DEVICE_ID);

    ProjectInfo info;
    auto json = ProjectSerializer::serializeProject(info);

    ProjectInfo loadedInfo;
    REQUIRE(ProjectSerializer::deserializeProject(json, loadedInfo));
    auto* loadedTrack = trackManager.getTrack(trackId);
    REQUIRE(loadedTrack != nullptr);
    REQUIRE(loadedTrack->chain.fxChainElements.size() == 1);
    REQUIRE(isDevice(loadedTrack->chain.fxChainElements[0]));
    const auto& loaded = getDevice(loadedTrack->chain.fxChainElements[0]);
    REQUIRE(loaded.modPanelOpen);
    REQUIRE(loaded.gainPanelOpen);
    REQUIRE(loaded.paramPanelOpen);
    REQUIRE(loaded.aiPanelOpen);
    REQUIRE(loaded.aiPanelOutput.isEmpty());
}

TEST_CASE("Section-scoped device ids survive project roundtrip",
          "[project][serialization][devices]") {
    ProjectTestFixture fixture;

    auto& trackManager = TrackManager::getInstance();
    auto trackId = trackManager.createTrack("Section IDs", TrackType::Audio);

    DeviceInfo fx;
    fx.name = "FX";
    fx.pluginId = "fx";
    DeviceInfo post;
    post.name = "Post";
    post.pluginId = "post";
    DeviceInfo analysis;
    analysis.name = "Analysis";
    analysis.pluginId = "oscilloscope";

    REQUIRE(trackManager.addDeviceToTrack(trackId, fx) == 1);
    REQUIRE(trackManager.addDeviceToPostFx(trackId, post) == 1);
    REQUIRE(trackManager.addDeviceToMixerAnalysis(trackId, analysis) == 1);

    ProjectInfo info;
    auto json = ProjectSerializer::serializeProject(info);

    ProjectInfo loadedInfo;
    REQUIRE(ProjectSerializer::deserializeProject(json, loadedInfo));

    auto* loadedTrack = trackManager.getTrack(trackId);
    REQUIRE(loadedTrack != nullptr);
    REQUIRE(loadedTrack->chain.fxChainElements.size() == 1);
    REQUIRE(loadedTrack->chain.postFxChainElements.size() == 1);
    REQUIRE(loadedTrack->chain.mixerAnalysisElements.size() == 1);
    REQUIRE(getDevice(loadedTrack->chain.fxChainElements[0]).id == 1);
    REQUIRE(loadedTrack->chain.postFxChainElements[0].device.id == 1);
    REQUIRE(loadedTrack->chain.mixerAnalysisElements[0].device.id == 1);

    fx.name = "FX 2";
    post.name = "Post 2";
    analysis.name = "Spectrum";
    analysis.pluginId = "spectrumanalyzer";

    REQUIRE(trackManager.addDeviceToTrack(trackId, fx) == 2);
    REQUIRE(trackManager.addDeviceToPostFx(trackId, post) == 2);
    REQUIRE(trackManager.addDeviceToMixerAnalysis(trackId, analysis) == 2);
}

TEST_CASE("Post-FX visible and mini mixer parameters survive project roundtrip",
          "[project][serialization][devices]") {
    ProjectTestFixture fixture;

    auto& trackManager = TrackManager::getInstance();
    auto trackId = trackManager.createTrack("Post FX Params", TrackType::Audio);

    DeviceInfo fx;
    fx.name = "FX";
    fx.pluginId = "fx";
    DeviceInfo post;
    post.name = "Post";
    post.pluginId = "post";

    const auto fxId = trackManager.addDeviceToTrack(trackId, fx);
    const auto postId = trackManager.addDeviceToPostFx(trackId, post);
    REQUIRE(fxId == 1);
    REQUIRE(postId == 1);

    const auto fxPath = ChainNodePath::topLevelDevice(trackId, fxId);
    const auto postPath = ChainNodePath::postFxDevice(trackId, postId);
    trackManager.setDeviceVisibleParameters(fxPath, {1, 2});
    trackManager.setDeviceMiniMixerParameters(fxPath, {3});
    trackManager.setDeviceVisibleParameters(postPath, {4, 5});
    trackManager.setDeviceMiniMixerParameters(postPath, {6, 7});

    ProjectInfo info;
    auto json = ProjectSerializer::serializeProject(info);

    ProjectInfo loadedInfo;
    REQUIRE(ProjectSerializer::deserializeProject(json, loadedInfo));

    auto* loadedFx = trackManager.getDeviceInChainByPath(fxPath);
    auto* loadedPost = trackManager.getDeviceInChainByPath(postPath);
    REQUIRE(loadedFx != nullptr);
    REQUIRE(loadedPost != nullptr);
    REQUIRE(loadedFx->visibleParameters == std::vector<int>{1, 2});
    REQUIRE(loadedFx->miniMixerParameters == std::vector<int>{3});
    REQUIRE(loadedPost->visibleParameters == std::vector<int>{4, 5});
    REQUIRE(loadedPost->miniMixerParameters == std::vector<int>{6, 7});
}

TEST_CASE("Mixer analysis plugin state survives project roundtrip",
          "[project][serialization][devices][pluginState]") {
    ProjectTestFixture fixture;

    auto& trackManager = TrackManager::getInstance();
    auto& projectManager = ProjectManager::getInstance();
    auto firstTrackId = trackManager.createTrack("First", TrackType::Audio);
    auto secondTrackId = trackManager.createTrack("Second", TrackType::Audio);

    DeviceInfo firstScope;
    firstScope.name = "Oscilloscope";
    firstScope.pluginId = "oscilloscope";
    firstScope.format = PluginFormat::Internal;
    firstScope.deviceType = DeviceType::Analysis;
    firstScope.pluginState = "<PLUGIN type=\"oscilloscope\" traceColour=\"1\"/>";

    DeviceInfo secondScope = firstScope;
    secondScope.pluginState = "<PLUGIN type=\"oscilloscope\" traceColour=\"5\"/>";

    REQUIRE(trackManager.addDeviceToMixerAnalysis(firstTrackId, firstScope) != INVALID_DEVICE_ID);
    REQUIRE(trackManager.addDeviceToMixerAnalysis(secondTrackId, secondScope) != INVALID_DEVICE_ID);

    auto tempFile = fixture.createTempProjectFile(".mgd");
    auto actualFile = ProjectTestFixture::wrappedPath(tempFile);
    REQUIRE(projectManager.saveProjectAs(tempFile));

    trackManager.clearAllTracks();
    REQUIRE(projectManager.loadProject(actualFile));

    auto* firstTrack = trackManager.getTrack(firstTrackId);
    auto* secondTrack = trackManager.getTrack(secondTrackId);
    REQUIRE(firstTrack != nullptr);
    REQUIRE(secondTrack != nullptr);
    REQUIRE(firstTrack->chain.mixerAnalysisElements.size() == 1);
    REQUIRE(secondTrack->chain.mixerAnalysisElements.size() == 1);
    REQUIRE(firstTrack->chain.mixerAnalysisElements[0].device.pluginState ==
            juce::String("<PLUGIN type=\"oscilloscope\" traceColour=\"1\"/>"));
    REQUIRE(secondTrack->chain.mixerAnalysisElements[0].device.pluginState ==
            juce::String("<PLUGIN type=\"oscilloscope\" traceColour=\"5\"/>"));
}

TEST_CASE("Master mixer analysis plugin state survives project roundtrip",
          "[project][serialization][master][pluginState]") {
    ProjectTestFixture fixture;

    auto& trackManager = TrackManager::getInstance();
    auto& projectManager = ProjectManager::getInstance();

    DeviceInfo scope;
    scope.name = "Oscilloscope";
    scope.pluginId = "oscilloscope";
    scope.format = PluginFormat::Internal;
    scope.deviceType = DeviceType::Analysis;
    scope.pluginState = "<PLUGIN type=\"oscilloscope\" traceColour=\"3\"/>";

    REQUIRE(trackManager.addDeviceToMixerAnalysis(MASTER_TRACK_ID, scope) != INVALID_DEVICE_ID);

    auto tempFile = fixture.createTempProjectFile(".mgd");
    auto actualFile = ProjectTestFixture::wrappedPath(tempFile);
    REQUIRE(projectManager.saveProjectAs(tempFile));

    trackManager.clearAllTracks();
    REQUIRE(projectManager.loadProject(actualFile));

    auto* masterTrack = trackManager.getTrack(MASTER_TRACK_ID);
    REQUIRE(masterTrack != nullptr);
    REQUIRE(masterTrack->chain.mixerAnalysisElements.size() == 1);
    REQUIRE(masterTrack->chain.mixerAnalysisElements[0].device.pluginState ==
            juce::String("<PLUGIN type=\"oscilloscope\" traceColour=\"3\"/>"));

    auto childTrackId = trackManager.createTrack("Later Track", TrackType::Audio);
    DeviceInfo siblingScope = scope;
    siblingScope.pluginState = "<PLUGIN type=\"oscilloscope\" traceColour=\"6\"/>";
    REQUIRE(trackManager.addDeviceToMixerAnalysis(childTrackId, siblingScope) == 2);
}

TEST_CASE("Post-fx device params are not automation targets",
          "[project][serialization][automation]") {
    ProjectTestFixture fixture;

    auto& trackManager = TrackManager::getInstance();
    auto& automationManager = AutomationManager::getInstance();
    auto trackId = trackManager.createTrack("Automation Section IDs", TrackType::Audio);

    DeviceInfo fx;
    fx.name = "FX";
    fx.pluginId = "fx";
    DeviceInfo post;
    post.name = "Post";
    post.pluginId = "post";

    auto fxId = trackManager.addDeviceToTrack(trackId, fx);
    auto postId = trackManager.addDeviceToPostFx(trackId, post);
    REQUIRE(fxId == 1);
    REQUIRE(postId == 1);

    juce::ignoreUnused(fxId);
    const auto postFxPath = ChainNodePath::postFxDevice(trackId, postId);
    const auto laneId = automationManager.createLane(ControlTarget::pluginParam(postFxPath, 0),
                                                     AutomationLaneType::Absolute);
    REQUIRE(laneId == INVALID_AUTOMATION_LANE_ID);

    ProjectInfo info;
    auto json = ProjectSerializer::serializeProject(info);

    ProjectInfo loadedInfo;
    REQUIRE(ProjectSerializer::deserializeProject(json, loadedInfo));

    const auto& lanes = automationManager.getLanes();
    REQUIRE(lanes.empty());
}

TEST_CASE("ParameterInfo display metadata roundtrip", "[project][serialization][parameter]") {
    ProjectTestFixture fixture;

    auto& trackManager = TrackManager::getInstance();
    auto trackId = trackManager.createTrack("Parameter Track", TrackType::Audio);

    ParameterInfo param;
    param.paramIndex = 12;
    param.name = "FB Bank";
    param.unit = "dB";
    param.minValue = -48.0f;
    param.maxValue = 12.0f;
    param.defaultValue = -6.0f;
    param.currentValue = -12.0f;
    param.teMinValue = 0.0f;
    param.teMaxValue = 1.0f;
    param.scale = ParameterScale::Logarithmic;
    param.skewFactor = 0.75f;
    param.scaleAnchor = -6.0f;
    param.displayFormat = DisplayFormat::Decibels;
    param.modulatable = false;
    param.bipolarModulation = true;
    param.choices = {"Off", "Low", "High"};
    param.labelTicks = {{0.0f, "Off"}, {2.0f, "High"}};
    param.valueTable = {"Off", "A", "B", "C"};
    param.gateSlotIndex = 7;
    param.gateNegated = true;
    param.hidden = true;
    param.displayText = std::make_shared<ParameterInfo::DisplayTextProvider>();

    DeviceInfo device;
    device.id = 52;
    device.name = "Feedback";
    device.pluginId = "internal.feedback";
    device.format = PluginFormat::Internal;
    device.parameters.push_back(param);
    REQUIRE(trackManager.addDeviceToTrack(trackId, device) != INVALID_DEVICE_ID);

    ProjectInfo info;
    auto json = ProjectSerializer::serializeProject(info);

    auto* rootObj = json.getDynamicObject();
    REQUIRE(rootObj != nullptr);
    auto* tracks = rootObj->getProperty("tracks").getArray();
    REQUIRE(tracks != nullptr);
    REQUIRE(tracks->size() == 1);
    auto* trackObj = tracks->getReference(0).getDynamicObject();
    REQUIRE(trackObj != nullptr);
    auto* elements = trackObj->getProperty("chainElements").getArray();
    REQUIRE(elements != nullptr);
    REQUIRE(elements->size() == 1);
    auto* elementObj = elements->getReference(0).getDynamicObject();
    REQUIRE(elementObj != nullptr);
    auto* deviceObj = elementObj->getProperty("device").getDynamicObject();
    REQUIRE(deviceObj != nullptr);
    auto* params = deviceObj->getProperty("parameters").getArray();
    REQUIRE(params != nullptr);
    REQUIRE(params->size() == 1);
    auto* paramObj = params->getReference(0).getDynamicObject();
    REQUIRE(paramObj != nullptr);
    REQUIRE(paramObj->hasProperty("teMinValue"));
    REQUIRE(paramObj->hasProperty("teMaxValue"));
    REQUIRE(paramObj->hasProperty("scaleAnchor"));
    REQUIRE(paramObj->hasProperty("displayFormat"));
    REQUIRE(paramObj->hasProperty("labelTicks"));
    REQUIRE(paramObj->hasProperty("valueTable"));
    REQUIRE(paramObj->hasProperty("gateSlotIndex"));
    REQUIRE(paramObj->hasProperty("gateNegated"));
    REQUIRE(paramObj->hasProperty("hidden"));

    ProjectInfo loadedInfo;
    REQUIRE(ProjectSerializer::deserializeProject(json, loadedInfo));
    auto* loadedTrack = trackManager.getTrack(trackId);
    REQUIRE(loadedTrack != nullptr);
    REQUIRE(loadedTrack->chain.fxChainElements.size() == 1);
    REQUIRE(isDevice(loadedTrack->chain.fxChainElements[0]));
    const auto& loadedDevice = getDevice(loadedTrack->chain.fxChainElements[0]);
    REQUIRE(loadedDevice.parameters.size() == 1);
    const auto& loaded = loadedDevice.parameters[0];
    REQUIRE(loaded.paramIndex == 12);
    REQUIRE(loaded.name == "FB Bank");
    REQUIRE(loaded.unit == "dB");
    REQUIRE(loaded.minValue == Approx(-48.0f));
    REQUIRE(loaded.maxValue == Approx(12.0f));
    REQUIRE(loaded.defaultValue == Approx(-6.0f));
    REQUIRE(loaded.currentValue == Approx(-12.0f));
    REQUIRE(loaded.teMinValue == Approx(0.0f));
    REQUIRE(loaded.teMaxValue == Approx(1.0f));
    REQUIRE(loaded.scale == ParameterScale::Logarithmic);
    REQUIRE(loaded.skewFactor == Approx(0.75f));
    REQUIRE(loaded.scaleAnchor == Approx(-6.0f));
    REQUIRE(loaded.displayFormat == DisplayFormat::Decibels);
    REQUIRE_FALSE(loaded.modulatable);
    REQUIRE(loaded.bipolarModulation);
    REQUIRE(loaded.choices.size() == 3);
    REQUIRE(loaded.choices[1] == "Low");
    REQUIRE(loaded.labelTicks.size() == 2);
    REQUIRE(loaded.labelTicks[1].first == Approx(2.0f));
    REQUIRE(loaded.labelTicks[1].second == "High");
    REQUIRE(loaded.valueTable.size() == 4);
    REQUIRE(loaded.valueTable[2] == "B");
    REQUIRE(loaded.gateSlotIndex == 7);
    REQUIRE(loaded.gateNegated);
    REQUIRE(loaded.hidden);
    REQUIRE_FALSE(static_cast<bool>(loaded.displayText));
}

TEST_CASE("MIDI controller curve metadata roundtrip", "[project][serialization][midi]") {
    ProjectTestFixture fixture;

    auto trackId = TrackManager::getInstance().createTrack("MIDI", TrackType::Audio);

    ClipInfo clip;
    clip.id = 51;
    clip.trackId = trackId;
    clip.name = "Curves";
    clip.setMidiContent();
    clip.setPlacementBeats(0.0, 4.0);

    MidiCCData cc;
    cc.controller = 74;
    cc.value = 96;
    cc.beatPosition = 1.5;
    cc.curveType = MidiCurveType::Bezier;
    cc.tension = 0.25;
    cc.inHandle = {-0.25, -0.1, false};
    cc.outHandle = {0.5, 0.2, false};
    clip.midiCCData.push_back(cc);

    MidiPitchBendData pb;
    pb.value = 9000;
    pb.beatPosition = 2.0;
    pb.curveType = MidiCurveType::Linear;
    pb.tension = -0.5;
    pb.inHandle = {-0.1, 0.3, true};
    pb.outHandle = {0.4, -0.2, false};
    clip.midiPitchBendData.push_back(pb);

    ClipManager::getInstance().restoreClip(clip);

    ProjectInfo info;
    info.tempo = 120.0;
    auto json = ProjectSerializer::serializeProject(info);

    ProjectInfo loadedInfo;
    REQUIRE(ProjectSerializer::deserializeProject(json, loadedInfo));
    auto* loaded = ClipManager::getInstance().getClip(clip.id);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->midiCCData.size() == 1);
    REQUIRE(loaded->midiCCData[0].curveType == MidiCurveType::Bezier);
    REQUIRE(loaded->midiCCData[0].tension == Approx(0.25));
    REQUIRE(loaded->midiCCData[0].inHandle.dx == Approx(-0.25));
    REQUIRE(loaded->midiCCData[0].inHandle.dy == Approx(-0.1));
    REQUIRE_FALSE(loaded->midiCCData[0].inHandle.linked);
    REQUIRE(loaded->midiCCData[0].outHandle.dx == Approx(0.5));
    REQUIRE(loaded->midiCCData[0].outHandle.dy == Approx(0.2));
    REQUIRE_FALSE(loaded->midiCCData[0].outHandle.linked);

    REQUIRE(loaded->midiPitchBendData.size() == 1);
    REQUIRE(loaded->midiPitchBendData[0].curveType == MidiCurveType::Linear);
    REQUIRE(loaded->midiPitchBendData[0].tension == Approx(-0.5));
    REQUIRE(loaded->midiPitchBendData[0].inHandle.dx == Approx(-0.1));
    REQUIRE(loaded->midiPitchBendData[0].inHandle.dy == Approx(0.3));
    REQUIRE(loaded->midiPitchBendData[0].inHandle.linked);
    REQUIRE(loaded->midiPitchBendData[0].outHandle.dx == Approx(0.4));
    REQUIRE(loaded->midiPitchBendData[0].outHandle.dy == Approx(-0.2));
    REQUIRE_FALSE(loaded->midiPitchBendData[0].outHandle.linked);
}

TEST_CASE("RackInfo panel UI state roundtrip", "[project][serialization][rack][ui_state]") {
    RackInfo rack;
    rack.id = 7;
    rack.name = "Panel Rack";
    rack.expanded = false;
    rack.modPanelOpen = true;
    rack.paramPanelOpen = true;

    ChainInfo chain;
    chain.id = 8;
    chain.name = "Chain 1";
    rack.chains.push_back(std::move(chain));

    auto json = ProjectSerializer::serializeRackInfo(rack);

    RackInfo loaded;
    REQUIRE(ProjectSerializer::deserializeRackInfo(json, loaded));

    REQUIRE(loaded.id == rack.id);
    REQUIRE(loaded.name == rack.name);
    REQUIRE(loaded.expanded == false);
    REQUIRE(loaded.modPanelOpen == true);
    REQUIRE(loaded.paramPanelOpen == true);
    REQUIRE(loaded.chains.size() == 1);
}
