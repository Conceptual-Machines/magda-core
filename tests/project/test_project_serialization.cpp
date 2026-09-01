#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>

#include "AudioClipTestHelpers.hpp"
#include "magda/daw/core/AppPaths.hpp"
#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/Config.hpp"
#include "magda/daw/core/MidiFileWriter.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/TrackPropertyCommands.hpp"
#include "magda/daw/core/UndoManager.hpp"
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

    bool hasActiveEdit() const override {
        return true;
    }

    BeatDuration getEditLengthBeats() const override {
        return {};
    }

    juce::File getEditFile() const override {
        return {};
    }

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

    const TempoMap* tempoMap() const override {
        return nullptr;
    }

    void setTimeSignature(int numerator, int denominator) override {
        timeSigNumerator = numerator;
        timeSigDenominator = denominator;
    }

    void getTimeSignature(int& numerator, int& denominator) const override {
        numerator = timeSigNumerator;
        denominator = timeSigDenominator;
    }

    void setLooping(bool enabled) override {
        ++setLoopingCalls;
        looping = enabled;
    }

    void setLoopRegionBeats(BeatRange range) override {
        loopStart = range.start.value;
        loopEnd = range.end.value;
    }

    bool isLooping() const override {
        return looping;
    }

    BeatRange getLoopRegionBeats() const override {
        return {{loopStart}, {loopEnd}};
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

    juce::BigInteger getEnabledWaveChannels(bool) const override {
        return {};
    }

    void setEnabledWaveChannels(bool, const juce::BigInteger&) override {}
    void rescanWaveDevices(bool, bool) override {}

    bool isDevicesLoading() const override {
        return false;
    }

    void setDevicesLoadingCallback(std::function<void(bool, const juce::String&)>) override {}

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

    MagdaApi& getMagdaApi() override {
        std::abort();
    }

    PluginWindowManager* getPluginWindowManager() override {
        return nullptr;
    }

    const PluginWindowManager* getPluginWindowManager() const override {
        return nullptr;
    }

    InsertRenderCaptureService* getInsertRenderCaptureService() override {
        return nullptr;
    }

    juce::Array<juce::PluginDescription> getKnownPluginTypes() const override {
        return {};
    }

    juce::Array<juce::PluginDescription> getPreferredPluginTypes() const override {
        return {};
    }

    void addPluginListChangeListener(juce::ChangeListener*) override {}
    void removePluginListChangeListener(juce::ChangeListener*) override {}
    void startPluginScan(std::function<void(float, const juce::String&)>) override {}
    void abortPluginScan() override {}

    void detectNewPlugins(std::function<void(PluginScanPhase, const juce::String&)>,
                          std::function<void(bool, int, int, const juce::StringArray&)>) override {}

    void setPluginScanCompletionCallback(
        std::function<void(bool, int, const juce::StringArray&)>) override {}

    bool isPluginScanRunning() const override {
        return false;
    }

    std::vector<ExcludedPlugin> getExcludedPlugins() const override {
        return {};
    }

    void setExcludedPlugins(const std::vector<ExcludedPlugin>&) override {}

    juce::File getPluginScanReportFile() const override {
        return {};
    }

    std::vector<std::string> getSystemPluginSearchPaths() const override {
        return {};
    }

    std::vector<ScannedPluginParameter> scanPluginParameters(const juce::String&, bool) override {
        return {};
    }

    bool upsertGrooveTemplate(const GrooveTemplateData&) override {
        return false;
    }

    juce::StringArray getGrooveTemplateNames() const override {
        return {};
    }

    std::unique_ptr<OfflineRenderSession> createOfflineRenderSession(bool) override {
        return nullptr;
    }

    std::vector<SamplerMediaReference> getSamplerMediaReferences() override {
        return {};
    }

    std::unique_ptr<UndoableCommand> createTempoSequenceRippleCommand(TempoSequenceRippleMode,
                                                                      BeatPosition,
                                                                      BeatPosition) override {
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
        loopStart = startTime;
        loopEnd = endTime;
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
        UndoManager::getInstance().clearHistory();
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
        UndoManager::getInstance().clearHistory();
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

        auto trackId = TrackManager::getInstance().createTrack("Audio Track", TrackType::Media);

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
        REQUIRE(magda::audioEventRef(staged.clips[0]).sourceFilePath() ==
                expectedFile.getFullPathName());
        REQUIRE(expectedFile.existsAsFile());
    }

    SECTION("Save As moves nested media content and relinks it") {
        // Stem splits nest one level below their root, and the pre-#2170
        // migration iterator was non-recursive, so they were left behind.
        auto& projectManager = ProjectManager::getInstance();
        REQUIRE(projectManager.newProject());

        auto trackId = TrackManager::getInstance().createTrack("Audio Track", TrackType::Media);

        auto splitDir = projectManager.getRendersDirectory().getChildFile("Vocals - htdemucs");
        REQUIRE(splitDir.createDirectory());
        auto stemFile = splitDir.getChildFile("vocals.wav");
        REQUIRE(stemFile.replaceWithText("placeholder audio"));

        auto clipId = ClipManager::getInstance().createAudioClipBeats(
            trackId, 0.0, 4.0, stemFile.getFullPathName(), ClipView::Arrangement, 120.0);
        REQUIRE(clipId != INVALID_CLIP_ID);

        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);
        REQUIRE(projectManager.saveProjectAs(tempFile));

        auto expectedFile = projectManager.getRendersDirectory()
                                .getChildFile("Vocals - htdemucs")
                                .getChildFile("vocals.wav");
        REQUIRE(expectedFile.existsAsFile());
        REQUIRE_FALSE(stemFile.existsAsFile());

        StagedProjectData staged;
        REQUIRE(ProjectSerializer::loadAndStage(actualFile, staged));
        REQUIRE(staged.clips.size() == 1);
        REQUIRE(magda::audioEventRef(staged.clips[0]).sourceFilePath() ==
                expectedFile.getFullPathName());
    }

    SECTION("A saved project declares exactly the three media roots") {
        auto& projectManager = ProjectManager::getInstance();
        REQUIRE(projectManager.newProject());

        auto tempFile = fixture.createTempProjectFile(".mgd");
        REQUIRE(projectManager.saveProjectAs(tempFile));

        auto mediaDir = projectManager.getMediaDirectory();
        REQUIRE(mediaDir.getChildFile("recordings").isDirectory());
        REQUIRE(mediaDir.getChildFile("renders").isDirectory());
        REQUIRE(mediaDir.getChildFile("imported").isDirectory());
        REQUIRE_FALSE(mediaDir.getChildFile("bounces").exists());
        REQUIRE_FALSE(mediaDir.getChildFile("external-edits").exists());
        REQUIRE_FALSE(mediaDir.getChildFile("stems").exists());
    }

    SECTION("Loading folds the retired media roots and relinks the clips") {
        auto& projectManager = ProjectManager::getInstance();
        REQUIRE(projectManager.newProject());

        auto trackId = TrackManager::getInstance().createTrack("Audio Track", TrackType::Media);

        // Build a project laid out the pre-#2170 way: the clips point straight
        // at the retired roots inside the media tree the save is about to
        // create, so nothing folds them until the load does.
        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);
        auto mediaDir = actualFile.getParentDirectory().getChildFile(
            actualFile.getFileNameWithoutExtension() + "_Media");

        auto bounceFile = mediaDir.getChildFile("bounces").getChildFile("kick.wav");
        auto editFile = mediaDir.getChildFile("external-edits").getChildFile("vox.wav");
        auto stemFile = mediaDir.getChildFile("stems")
                            .getChildFile("Vocals - htdemucs")
                            .getChildFile("vocals.wav");
        for (const auto& legacyFile : {bounceFile, editFile, stemFile}) {
            REQUIRE(legacyFile.getParentDirectory().createDirectory());
            REQUIRE(legacyFile.replaceWithText("placeholder audio"));
        }

        for (const auto& legacyFile : {bounceFile, editFile, stemFile}) {
            REQUIRE(ClipManager::getInstance().createAudioClipBeats(
                        trackId, 0.0, 4.0, legacyFile.getFullPathName(), ClipView::Arrangement,
                        120.0) != INVALID_CLIP_ID);
        }

        REQUIRE(projectManager.saveProjectAs(tempFile));
        REQUIRE(bounceFile.existsAsFile());

        REQUIRE(projectManager.loadProject(actualFile));

        // Retired roots are gone, their content folded into the survivors.
        REQUIRE_FALSE(mediaDir.getChildFile("bounces").exists());
        REQUIRE_FALSE(mediaDir.getChildFile("external-edits").exists());
        REQUIRE_FALSE(mediaDir.getChildFile("stems").exists());

        auto foldedBounce = mediaDir.getChildFile("renders").getChildFile("kick.wav");
        auto foldedEdit = mediaDir.getChildFile("imported").getChildFile("vox.wav");
        auto foldedStem = mediaDir.getChildFile("renders")
                              .getChildFile("Vocals - htdemucs")
                              .getChildFile("vocals.wav");
        REQUIRE(foldedBounce.existsAsFile());
        REQUIRE(foldedEdit.existsAsFile());
        REQUIRE(foldedStem.existsAsFile());

        juce::StringArray clipPaths;
        for (const auto& clip : ClipManager::getInstance().getClips())
            if (clip.isAudio())
                clipPaths.add(magda::audioEventRef(clip).sourceFilePath());
        REQUIRE(clipPaths.size() == 3);
        REQUIRE(clipPaths.contains(foldedBounce.getFullPathName()));
        REQUIRE(clipPaths.contains(foldedEdit.getFullPathName()));
        REQUIRE(clipPaths.contains(foldedStem.getFullPathName()));

        // The .mgd on disk still names the folders that just went away.
        REQUIRE(projectManager.isDirty());

        // Saving is what makes the fold durable, and it leaves the project
        // clean again. Left dirty, the next test's newProject() would block on
        // the unsaved-changes dialog, since ProjectManager is a singleton and
        // Catch2 sections share it.
        REQUIRE(projectManager.saveProject());
        REQUIRE_FALSE(projectManager.isDirty());

        StagedProjectData staged;
        REQUIRE(ProjectSerializer::loadAndStage(actualFile, staged));
        juce::StringArray savedPaths;
        for (const auto& clip : staged.clips)
            if (clip.isAudio())
                savedPaths.add(magda::audioEventRef(clip).sourceFilePath());
        REQUIRE(savedPaths.contains(foldedBounce.getFullPathName()));
        REQUIRE(savedPaths.contains(foldedEdit.getFullPathName()));
        REQUIRE(savedPaths.contains(foldedStem.getFullPathName()));
    }

    SECTION("Plugin state is captured after the media migration") {
        // onBeforeSave snapshots sampler and drum-pad sample paths. Capturing
        // it before the migration would freeze the pre-move paths into the
        // .mgd, leaving those samples missing when the project is reopened.
        auto& projectManager = ProjectManager::getInstance();
        REQUIRE(projectManager.newProject());

        auto sourceFile = projectManager.getImportedDirectory().getChildFile("sample.wav");
        REQUIRE(sourceFile.getParentDirectory().createDirectory());
        REQUIRE(sourceFile.replaceWithText("placeholder audio"));

        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);
        auto expectedFile = actualFile.getParentDirectory()
                                .getChildFile(actualFile.getFileNameWithoutExtension() + "_Media")
                                .getChildFile("imported")
                                .getChildFile("sample.wav");

        bool sawMigratedMedia = false;
        projectManager.onBeforeSave = [&sawMigratedMedia, expectedFile]() {
            sawMigratedMedia = expectedFile.existsAsFile();
        };
        const bool saved = projectManager.saveProjectAs(tempFile);
        projectManager.onBeforeSave = nullptr;

        REQUIRE(saved);
        REQUIRE(sawMigratedMedia);
    }

    SECTION("Folding leaves a colliding legacy file where it is") {
        auto& projectManager = ProjectManager::getInstance();
        REQUIRE(projectManager.newProject());

        auto trackId = TrackManager::getInstance().createTrack("Audio Track", TrackType::Media);

        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);
        auto mediaDir = actualFile.getParentDirectory().getChildFile(
            actualFile.getFileNameWithoutExtension() + "_Media");

        auto renderFile = mediaDir.getChildFile("renders").getChildFile("kick.wav");
        auto bounceFile = mediaDir.getChildFile("bounces").getChildFile("kick.wav");
        REQUIRE(renderFile.getParentDirectory().createDirectory());
        REQUIRE(bounceFile.getParentDirectory().createDirectory());
        REQUIRE(renderFile.replaceWithText("the render"));
        REQUIRE(bounceFile.replaceWithText("the bounce"));

        REQUIRE(ClipManager::getInstance().createAudioClipBeats(
                    trackId, 0.0, 4.0, bounceFile.getFullPathName(), ClipView::Arrangement,
                    120.0) != INVALID_CLIP_ID);

        REQUIRE(projectManager.saveProjectAs(tempFile));
        REQUIRE(projectManager.loadProject(actualFile));

        // Nothing overwritten, nothing renamed, nothing substituted: the clip
        // keeps pointing at the path its .mgd already names, so a later load
        // that finds the folder gone cannot mistake the render for the bounce.
        REQUIRE(bounceFile.existsAsFile());
        REQUIRE(bounceFile.loadFileAsString() == "the bounce");
        REQUIRE(renderFile.loadFileAsString() == "the render");
        REQUIRE_FALSE(mediaDir.getChildFile("renders").getChildFile("kick_2.wav").exists());

        auto clips = ClipManager::getInstance().getClips();
        REQUIRE(clips.size() == 1);
        REQUIRE(magda::audioEventRef(clips[0]).sourceFilePath() == bounceFile.getFullPathName());

        // Nothing moved, so there is nothing the .mgd is out of date about.
        REQUIRE_FALSE(projectManager.isDirty());
    }

    SECTION("A folded path resolves on reload from the recorded move") {
        // The fold's new paths only reach the .mgd when the user saves. Until
        // then the next load has to learn where the file went from the record
        // the fold left behind.
        auto& projectManager = ProjectManager::getInstance();
        REQUIRE(projectManager.newProject());

        auto trackId = TrackManager::getInstance().createTrack("Audio Track", TrackType::Media);

        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);
        auto mediaDir = actualFile.getParentDirectory().getChildFile(
            actualFile.getFileNameWithoutExtension() + "_Media");

        auto bounceFile = mediaDir.getChildFile("bounces").getChildFile("kick.wav");
        REQUIRE(bounceFile.getParentDirectory().createDirectory());
        REQUIRE(bounceFile.replaceWithText("the bounce"));

        REQUIRE(ClipManager::getInstance().createAudioClipBeats(
                    trackId, 0.0, 4.0, bounceFile.getFullPathName(), ClipView::Arrangement,
                    120.0) != INVALID_CLIP_ID);
        REQUIRE(projectManager.saveProjectAs(tempFile));

        // Stand in for a previous load that folded and was never saved: the
        // file has moved and the record says so, but the .mgd still names the
        // old location.
        auto foldedFile = mediaDir.getChildFile("renders").getChildFile("kick.wav");
        REQUIRE(foldedFile.getParentDirectory().createDirectory());
        REQUIRE(bounceFile.moveFileTo(foldedFile));
        mediaDir.getChildFile("bounces").deleteRecursively();
        REQUIRE(mediaDir.getChildFile(".magda-media-moves.json")
                    .replaceWithText(R"({"bounces/kick.wav": "renders/kick.wav"})"));

        REQUIRE(projectManager.loadProject(actualFile));

        auto clips = ClipManager::getInstance().getClips();
        REQUIRE(clips.size() == 1);
        REQUIRE(magda::audioEventRef(clips[0]).sourceFilePath() == foldedFile.getFullPathName());

        REQUIRE(projectManager.saveProject());
    }

    SECTION("A legacy path missing before any fold is left alone") {
        // Nothing moved it, so nothing knows where it went. A file in the
        // replacement root that happens to share its name is a different file,
        // and relinking to it would silently play unrelated audio.
        auto& projectManager = ProjectManager::getInstance();
        REQUIRE(projectManager.newProject());

        auto trackId = TrackManager::getInstance().createTrack("Audio Track", TrackType::Media);

        auto tempFile = fixture.createTempProjectFile(".mgd");
        auto actualFile = ProjectTestFixture::wrappedPath(tempFile);
        auto mediaDir = actualFile.getParentDirectory().getChildFile(
            actualFile.getFileNameWithoutExtension() + "_Media");

        auto bounceFile = mediaDir.getChildFile("bounces").getChildFile("ghost.wav");
        REQUIRE(bounceFile.getParentDirectory().createDirectory());
        REQUIRE(bounceFile.replaceWithText("the bounce"));

        REQUIRE(ClipManager::getInstance().createAudioClipBeats(
                    trackId, 0.0, 4.0, bounceFile.getFullPathName(), ClipView::Arrangement,
                    120.0) != INVALID_CLIP_ID);
        REQUIRE(projectManager.saveProjectAs(tempFile));

        // The referenced bounce is gone — deleted outside MAGDA, say — and an
        // unrelated render happens to carry the same name.
        REQUIRE(bounceFile.deleteFile());
        mediaDir.getChildFile("bounces").deleteRecursively();
        auto unrelated = mediaDir.getChildFile("renders").getChildFile("ghost.wav");
        REQUIRE(unrelated.getParentDirectory().createDirectory());
        REQUIRE(unrelated.replaceWithText("a different render"));

        REQUIRE(projectManager.loadProject(actualFile));

        auto clips = ClipManager::getInstance().getClips();
        REQUIRE(clips.size() == 1);
        REQUIRE(magda::audioEventRef(clips[0]).sourceFilePath() == bounceFile.getFullPathName());
        REQUIRE(unrelated.loadFileAsString() == "a different render");
        REQUIRE_FALSE(projectManager.isDirty());
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

    auto trackId = TrackManager::getInstance().createTrack("Audio", TrackType::Media);

    ClipInfo clip;
    clip.id = 42;
    clip.trackId = trackId;
    clip.name = "Loop";
    clip.setAudioContent();
    clip.setPlacementBeats(4.0, 16.0);
    magda::test::giveAudioEvent(clip, "/tmp/loop.wav");
    magda::test::setSourceDuration(clip, 2.7907);
    magda::test::audioEvent(clip).interpBpm = 172.0;
    magda::test::audioEvent(clip).interpTotalBeats = 8.0;
    magda::test::audioEvent(clip).interpTotalBeatsLocked = true;
    magda::test::audioEvent(clip).autoTempo = true;
    clip.loopEnabled = true;
    magda::test::audioEvent(clip).setLoopStartBeats(0.0);
    magda::test::audioEvent(clip).setLoopLengthBeats(8.0);
    magda::test::audioEvent(clip).setLoopLengthSeconds(8.0 * 60.0 / 172.0);
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

    // Schema v2 (#1901): the clip holds events, the sources live once per
    // project, and the clip no longer carries source/interpretation/playback.
    REQUIRE(static_cast<int>(rootObj->getProperty("schemaVersion")) == kProjectSchemaVersion);

    auto* sources = rootObj->getProperty("sources").getArray();
    REQUIRE(sources != nullptr);
    REQUIRE(sources->size() == 1);
    auto* sourceObj = sources->getReference(0).getDynamicObject();
    REQUIRE(sourceObj != nullptr);
    REQUIRE(sourceObj->getProperty("filePath").toString() == "/tmp/loop.wav");
    REQUIRE(static_cast<double>(sourceObj->getProperty("durationSeconds")) == Approx(2.7907));

    auto* audioObj = clipObj->getProperty("audio").getDynamicObject();
    REQUIRE(audioObj != nullptr);
    REQUIRE(audioObj->getProperty("source").isVoid());
    REQUIRE(audioObj->getProperty("interpretation").isVoid());
    REQUIRE(audioObj->getProperty("playback").isVoid());

    auto* events = audioObj->getProperty("events").getArray();
    REQUIRE(events != nullptr);
    REQUIRE(events->size() == 1);
    auto* eventObj = events->getReference(0).getDynamicObject();
    REQUIRE(eventObj != nullptr);
    REQUIRE(static_cast<int>(eventObj->getProperty("sourceId")) ==
            static_cast<int>(sourceObj->getProperty("id")));
    REQUIRE(static_cast<double>(eventObj->getProperty("interpTotalBeats")) == Approx(8.0));
    REQUIRE(static_cast<bool>(eventObj->getProperty("interpTotalBeatsLocked")));
    // The loop toggle is a clip property and did not move onto the event.
    REQUIRE(static_cast<bool>(clipObj->getProperty("loopEnabled")));

    // The event spans its clip: container and content are the same extent.
    REQUIRE(static_cast<double>(eventObj->getProperty("startBeat")) == Approx(0.0));
    REQUIRE(static_cast<double>(eventObj->getProperty("lengthBeats")) == Approx(16.0));

    auto* placementObj = clipObj->getProperty("placement").getDynamicObject();
    REQUIRE(placementObj != nullptr);
    REQUIRE(static_cast<double>(placementObj->getProperty("lengthBeats")) == Approx(16.0));

    ProjectInfo loaded;
    REQUIRE(ProjectSerializer::deserializeProject(json, loaded));
    auto* restored = ClipManager::getInstance().getClip(clip.id);
    REQUIRE(restored != nullptr);
    REQUIRE(restored->isAudio());
    REQUIRE(restored->placement.lengthBeats == Approx(16.0));
    REQUIRE(primaryEventOf(restored)->sourceDurationSeconds() == Approx(2.7907));
    REQUIRE(primaryEventOf(restored)->interpTotalBeats == Approx(8.0));
    REQUIRE(primaryEventOf(restored)->interpTotalBeatsLocked);
    REQUIRE(primaryEventOf(restored)->loopLengthBeats() == Approx(8.0));
}

TEST_CASE("Session clip follow action settings roundtrip", "[project][serialization][session]") {
    ProjectTestFixture fixture;

    auto trackId = TrackManager::getInstance().createTrack("MIDI", TrackType::Media);

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

TEST_CASE("Clip enabled state roundtrips and missing property defaults to enabled",
          "[project][serialization][clip]") {
    ProjectTestFixture fixture;

    auto trackId = TrackManager::getInstance().createTrack("MIDI", TrackType::Media);

    ClipInfo clip;
    clip.id = 93;
    clip.trackId = trackId;
    clip.name = "Disabled";
    clip.setMidiContent();
    clip.view = ClipView::Arrangement;
    clip.setPlacementBeats(0.0, 4.0);
    clip.enabled = false;
    ClipManager::getInstance().restoreClip(clip);

    ProjectInfo info;
    info.name = "Enabled Toggle";
    info.tempo = 120.0;

    auto json = ProjectSerializer::serializeProject(info);
    auto* rootObj = json.getDynamicObject();
    REQUIRE(rootObj != nullptr);
    auto* clips = rootObj->getProperty("clips").getArray();
    REQUIRE(clips != nullptr);
    REQUIRE(clips->size() == 1);
    auto* clipObj = clips->getReference(0).getDynamicObject();
    REQUIRE(clipObj != nullptr);
    REQUIRE(static_cast<bool>(clipObj->getProperty("enabled")) == false);

    ProjectInfo loaded;
    REQUIRE(ProjectSerializer::deserializeProject(json, loaded));
    auto* restored = ClipManager::getInstance().getClip(clip.id);
    REQUIRE(restored != nullptr);
    REQUIRE_FALSE(restored->enabled);

    // Projects saved before the flag existed have no "enabled" property —
    // they must load with the clip enabled.
    clipObj->removeProperty("enabled");
    ProjectInfo legacy;
    REQUIRE(ProjectSerializer::deserializeProject(json, legacy));
    restored = ClipManager::getInstance().getClip(clip.id);
    REQUIRE(restored != nullptr);
    REQUIRE(restored->enabled);
}

TEST_CASE("Looped MIDI clip serialization preserves loop region separate from placement",
          "[project][serialization][midi][loop]") {
    ProjectTestFixture fixture;

    auto trackId = TrackManager::getInstance().createTrack("MIDI", TrackType::Media);

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
    REQUIRE(restored->midiOffset == Approx(1.0));
    REQUIRE(restored->midi().sourceFilePath == "/tmp/imported-loop.mid");
}

TEST_CASE("A bent pitch glide roundtrips, and a straight one writes nothing extra",
          "[project][serialization][midi]") {
    // #2198 gave each glide segment a shape. The shape has to survive a save,
    // and a project full of straight glides has to come back byte-identical to
    // what it was before the field existed -- which is what the write-only-when-
    // bent rule buys and what a reader of old projects depends on.
    ProjectTestFixture fixture;

    auto trackId = TrackManager::getInstance().createTrack("MIDI", TrackType::Media);

    ClipInfo clip;
    clip.id = 93;
    clip.trackId = trackId;
    clip.setMidiContent();
    clip.view = ClipView::Arrangement;
    clip.setPlacementBeats(0.0, 4.0);

    MidiNote note;
    note.noteNumber = 60;
    note.startBeat = 0.0;
    note.lengthBeats = 4.0;
    note.pitchExpression = {MidiPitchExpressionPoint{0.0, 0.0, 1.75},
                            MidiPitchExpressionPoint{2.0, 5.0, 0.0},
                            MidiPitchExpressionPoint{4.0, -3.0, -2.5}};
    clip.midiNotes.push_back(note);

    ClipManager::getInstance().restoreClip(clip);

    ProjectInfo info;
    info.name = "Bent Glide";
    info.tempo = 120.0;

    auto json = ProjectSerializer::serializeProject(info);

    // The straight middle segment carries no tension property at all.
    auto* rootObj = json.getDynamicObject();
    REQUIRE(rootObj != nullptr);
    auto* clips = rootObj->getProperty("clips").getArray();
    REQUIRE(clips != nullptr);
    REQUIRE(clips->size() == 1);
    auto* clipObj = clips->getReference(0).getDynamicObject();
    REQUIRE(clipObj != nullptr);
    auto* notes = clipObj->getProperty("midiNotes").getArray();
    REQUIRE(notes != nullptr);
    REQUIRE(notes->size() == 1);
    auto* noteObj = notes->getReference(0).getDynamicObject();
    REQUIRE(noteObj != nullptr);
    auto* pointArray = noteObj->getProperty("pitchExpression").getArray();
    REQUIRE(pointArray != nullptr);
    REQUIRE(pointArray->size() == 3);
    CHECK(pointArray->getReference(0).getDynamicObject()->hasProperty("tension"));
    CHECK_FALSE(pointArray->getReference(1).getDynamicObject()->hasProperty("tension"));
    CHECK(pointArray->getReference(2).getDynamicObject()->hasProperty("tension"));

    ProjectInfo loaded;
    REQUIRE(ProjectSerializer::deserializeProject(json, loaded));
    const auto* restored = ClipManager::getInstance().getClip(clip.id);
    REQUIRE(restored != nullptr);
    REQUIRE(restored->midiNotes.size() == 1);

    const auto& points = restored->midiNotes.front().pitchExpression;
    REQUIRE(points.size() == 3);
    CHECK(points[0].tension == Approx(1.75));
    CHECK(points[1].tension == Approx(0.0));
    CHECK(points[2].tension == Approx(-2.5));
    CHECK(points[1].semitones == Approx(5.0));
}

TEST_CASE("Saving a MIDI clip to the media library writes and indexes a generated source file",
          "[project][serialization][midi][media_db]") {
    ProjectTestFixture fixture;
    ScopedTestDataDir dataDir("magda-midi-library-test");

    auto trackId = TrackManager::getInstance().createTrack("MIDI Track", TrackType::Media);
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

    auto trackId = TrackManager::getInstance().createTrack("Audio Track", TrackType::Media);
    auto clipId = ClipManager::getInstance().createAudioClip(
        trackId, 0.0, 4.0, sourceFile.getFullPathName(), ClipView::Arrangement, 120.0);
    REQUIRE(clipId != INVALID_CLIP_ID);

    auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->isAudio());

    primaryEventOf(clip)->autoTempo = true;
    primaryEventOf(clip)->interpBpm = 140.0;
    primaryEventOf(clip)->interpTotalBeats = 9.3333333333;
    primaryEventOf(clip)->interpTotalBeatsLocked = true;
    primaryEventOf(clip)->warpEnabled = true;
    primaryEventOf(clip)->warpMarkers = {{0.0, 0.0}, {1.234, 1.75}, {3.5, 4.0}};

    REQUIRE(ClipManager::getInstance().saveClipToLibrary(clipId));

    ClipManager::getInstance().clearAllClips();
    auto reimportedId = ClipManager::getInstance().createAudioClip(
        trackId, 0.0, 4.0, sourceFile.getFullPathName(), ClipView::Arrangement, 120.0);
    REQUIRE(reimportedId != INVALID_CLIP_ID);

    auto* reimported = ClipManager::getInstance().getClip(reimportedId);
    REQUIRE(reimported != nullptr);
    REQUIRE(reimported->isAudio());
    REQUIRE(primaryEventOf(reimported)->warpEnabled);
    REQUIRE(primaryEventOf(reimported)->interpBpm == Approx(140.0));
    REQUIRE(primaryEventOf(reimported)->interpTotalBeats == Approx(9.3333333333));
    REQUIRE(primaryEventOf(reimported)->warpMarkers.size() == 3);
    REQUIRE(primaryEventOf(reimported)->warpMarkers[1].sourceTime == Approx(1.234));
    REQUIRE(primaryEventOf(reimported)->warpMarkers[1].warpTime == Approx(1.75));
    REQUIRE(primaryEventOf(reimported)->warpMarkers[2].sourceTime == Approx(3.5));
    REQUIRE(primaryEventOf(reimported)->warpMarkers[2].warpTime == Approx(4.0));
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

    auto trackId = TrackManager::getInstance().createTrack("Track", TrackType::Media);

    ClipInfo clip;
    clip.id = 7;
    clip.trackId = trackId;
    clip.name = "Schema";
    clip.setAudioContent();
    clip.setPlacementBeats(0.0, 16.0);
    magda::test::giveAudioEvent(clip, "/tmp/schema.wav");
    magda::test::setSourceDuration(clip, 4.0);
    magda::test::audioEvent(clip).interpBpm = 120.0;
    magda::test::audioEvent(clip).interpTotalBeats = 8.0;
    magda::test::audioEvent(clip).autoTempo = true;
    clip.loopEnabled = true;
    magda::test::audioEvent(clip).setLoopLengthBeats(8.0);
    magda::test::audioEvent(clip).setLoopLengthSeconds(4.0);
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
        REQUIRE(primaryEventOf(restored)->sourceFilePath() == "/tmp/legacy.wav");
        REQUIRE(primaryEventOf(restored)->sourceDurationSeconds() == Approx(4.0));
        REQUIRE(primaryEventOf(restored)->interpTotalBeats == Approx(8.0));
        REQUIRE(primaryEventOf(restored)->interpBpm == Approx(120.0));
        REQUIRE(primaryEventOf(restored)->anchorSeconds() == Approx(0.5));
        REQUIRE(primaryEventOf(restored)->anchorBeats() == Approx(1.0));
        REQUIRE(primaryEventOf(restored)->loopStartSeconds() == Approx(0.25));
        REQUIRE(primaryEventOf(restored)->loopLengthSeconds() == Approx(4.0));
        REQUIRE(primaryEventOf(restored)->loopStartBeats() == Approx(0.5));
        REQUIRE(primaryEventOf(restored)->loopLengthBeats() == Approx(8.0));
        REQUIRE(primaryEventOf(restored)->speedRatio == Approx(1.25));
        REQUIRE(primaryEventOf(restored)->warpEnabled);
        REQUIRE(primaryEventOf(restored)->warpMarkers.size() == 1);
        REQUIRE(primaryEventOf(restored)->warpMarkers.front().sourceTime == Approx(1.0));
        REQUIRE(primaryEventOf(restored)->warpMarkers.front().warpTime == Approx(1.25));
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
        REQUIRE(primaryEventOf(restored)->sourceFilePath() == "/tmp/flat-legacy.wav");
        REQUIRE(primaryEventOf(restored)->sourceDurationSeconds() == Approx(6.0));
        REQUIRE(primaryEventOf(restored)->interpTotalBeats == Approx(12.0));
        REQUIRE(primaryEventOf(restored)->interpBpm == Approx(120.0));
        REQUIRE(primaryEventOf(restored)->anchorSeconds() == Approx(0.25));
        REQUIRE(primaryEventOf(restored)->anchorBeats() == Approx(0.5));
        REQUIRE(primaryEventOf(restored)->loopStartSeconds() == Approx(0.125));
        REQUIRE(primaryEventOf(restored)->loopLengthSeconds() == Approx(6.0));
        REQUIRE(primaryEventOf(restored)->loopStartBeats() == Approx(0.25));
        REQUIRE(primaryEventOf(restored)->loopLengthBeats() == Approx(12.0));
        REQUIRE(primaryEventOf(restored)->speedRatio == Approx(1.5));
    }
}

TEST_CASE("Automation serialization uses beat-domain property names",
          "[project][serialization][automation][beats]") {
    ProjectTestFixture fixture;

    auto trackId = TrackManager::getInstance().createTrack("Automation", TrackType::Media);
    auto& automation = AutomationManager::getInstance();
    const auto target = ControlTarget::trackVolume(trackId);
    auto laneId = automation.createLane(target, AutomationLaneType::ClipBased);
    automation.setLaneSnapEditsToBeatGrid(laneId, false);
    automation.beginTargetGesture(target);
    REQUIRE(automation.getLane(laneId)->authorityState == AutomationAuthorityState::Touching);
    auto clipId = automation.createClip(laneId, 4.0, 8.0);
    REQUIRE(clipId != INVALID_AUTOMATION_CLIP_ID);
    automation.setClipLooping(clipId, true);
    automation.setClipLoopLength(clipId, 2.0);
    automation.setClipSnapX(clipId, false, 1, 16);
    automation.setClipSnapY(clipId, true, 1, 12);
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
    REQUIRE(laneObj->hasProperty("bypass"));
    REQUIRE_FALSE(static_cast<bool>(laneObj->getProperty("bypass")));
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
    REQUIRE(static_cast<bool>(obj->getProperty("snapXEnabled")) == false);
    REQUIRE(static_cast<int>(obj->getProperty("snapXNumerator")) == 1);
    REQUIRE(static_cast<int>(obj->getProperty("snapXDenominator")) == 16);
    REQUIRE(static_cast<bool>(obj->getProperty("snapYEnabled")) == true);
    REQUIRE(static_cast<int>(obj->getProperty("snapYNumerator")) == 1);
    REQUIRE(static_cast<int>(obj->getProperty("snapYDenominator")) == 12);

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
    REQUIRE(restoredClips[0].snapXEnabled == false);
    REQUIRE(restoredClips[0].snapXNumerator == 1);
    REQUIRE(restoredClips[0].snapXDenominator == 16);
    REQUIRE(restoredClips[0].snapYEnabled == true);
    REQUIRE(restoredClips[0].snapYNumerator == 1);
    REQUIRE(restoredClips[0].snapYDenominator == 12);
    REQUIRE(restoredClips[0].points.size() == 1);
    REQUIRE(restoredClips[0].points[0].beatPosition == Approx(1.5));
    REQUIRE(restoredClips[0].points[0].inHandle.beatOffset == Approx(-0.25));
    REQUIRE(restoredClips[0].points[0].outHandle.beatOffset == Approx(0.5));
    const auto* restoredLane = AutomationManager::getInstance().getLane(laneId);
    REQUIRE(restoredLane != nullptr);
    REQUIRE_FALSE(restoredLane->snapEditsToBeatGrid);
    REQUIRE(restoredLane->authorityState == AutomationAuthorityState::Reading);
}

TEST_CASE("Automation serialization persists only explicit disablement",
          "[project][serialization][automation][authority]") {
    ProjectTestFixture fixture;

    const auto trackId = TrackManager::getInstance().createTrack("Automation", TrackType::Media);
    auto& automation = AutomationManager::getInstance();
    const auto laneId =
        automation.createLane(ControlTarget::trackVolume(trackId), AutomationLaneType::Absolute);
    automation.setLaneEnabled(laneId, false);

    ProjectInfo info;
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
    REQUIRE(static_cast<bool>(laneObj->getProperty("bypass")));

    REQUIRE(ProjectSerializer::deserializeProject(json, info));
    const auto* restoredLane = AutomationManager::getInstance().getLane(laneId);
    REQUIRE(restoredLane != nullptr);
    REQUIRE(restoredLane->authorityState == AutomationAuthorityState::Disabled);
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
        trackManager.createTrack("Audio 1", TrackType::Media);
        trackManager.createTrack("MIDI 1", TrackType::Media);

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
        REQUIRE(tracks[0].type == TrackType::Media);
        REQUIRE(tracks[1].name == "MIDI 1");
        REQUIRE(tracks[1].type == TrackType::Media);

        // Cleanup
        trackManager.clearAllTracks();
    }
}

TEST_CASE("normalizeForType clears input state on input-less tracks",
          "[project][tracks][invariants]") {
    SECTION("Group track loses external input, monitor, and record-arm") {
        TrackInfo group;
        group.type = TrackType::Group;
        group.recordArmed = true;
        group.inputMonitor = InputMonitorMode::In;
        group.midiInputDevice = "all";
        group.audioInputDevice = "input:1";

        group.normalizeForType();

        REQUIRE(group.recordArmed == false);
        REQUIRE(group.inputMonitor == InputMonitorMode::Off);
        REQUIRE(group.midiInputDevice.isEmpty());
        REQUIRE(group.audioInputDevice.isEmpty());
    }

    SECTION("Aux track loses external input, monitor, and record-arm") {
        TrackInfo aux;
        aux.type = TrackType::Aux;
        aux.recordArmed = true;
        aux.inputMonitor = InputMonitorMode::Auto;
        aux.midiInputDevice = "all";
        aux.audioInputDevice = "input:2";

        aux.normalizeForType();

        REQUIRE(aux.recordArmed == false);
        REQUIRE(aux.inputMonitor == InputMonitorMode::Off);
        REQUIRE(aux.midiInputDevice.isEmpty());
        REQUIRE(aux.audioInputDevice.isEmpty());
    }

    SECTION("Input-taking track keeps its input state") {
        TrackInfo audio;
        audio.type = TrackType::Media;
        audio.recordArmed = true;
        audio.inputMonitor = InputMonitorMode::In;
        audio.midiInputDevice = "all";
        audio.audioInputDevice = "input:3";

        audio.normalizeForType();

        REQUIRE(audio.recordArmed == true);
        REQUIRE(audio.inputMonitor == InputMonitorMode::In);
        REQUIRE(audio.midiInputDevice == "all");
        REQUIRE(audio.audioInputDevice == "input:3");
    }
}

TEST_CASE("Restoring a legacy group track normalizes stale input state",
          "[project][tracks][invariants][restore]") {
    ProjectTestFixture fixture;
    auto& trackManager = TrackManager::getInstance();

    // Simulate a group track saved before the input-less invariant existed:
    // it carries record-arm, input monitoring, and MIDI/audio input routing.
    TrackInfo legacyGroup;
    legacyGroup.id = 4200;
    legacyGroup.type = TrackType::Group;
    legacyGroup.name = "Legacy Group";
    legacyGroup.recordArmed = true;
    legacyGroup.inputMonitor = InputMonitorMode::In;
    legacyGroup.midiInputDevice = "all";
    legacyGroup.audioInputDevice = "input:1";

    trackManager.restoreTrack(legacyGroup);

    const auto* restored = trackManager.getTrack(4200);
    REQUIRE(restored != nullptr);
    REQUIRE(restored->type == TrackType::Group);
    REQUIRE(restored->recordArmed == false);
    REQUIRE(restored->inputMonitor == InputMonitorMode::Off);
    REQUIRE(restored->midiInputDevice.isEmpty());
    REQUIRE(restored->audioInputDevice.isEmpty());
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
        trackManager.createTrack("Test", TrackType::Media);
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
        auto trackId = trackManager.createTrack("Test MIDI Track", TrackType::Media);
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
        REQUIRE(tracks[0].type == TrackType::Media);

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
        auto trackId = trackManager.createTrack("Test Audio Track", TrackType::Media);

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

TEST_CASE("Project title and credits roundtrip", "[project][serialization][metadata]") {
    ProjectTestFixture fixture;

    // Every field gets a value derived from its own key, so a mapping that
    // crosses two fields over reads as the wrong string rather than passing by
    // coincidence.
    auto filled = []() {
        ProjectMetadata metadata;
        for (const auto& field : kProjectMetadataFields)
            metadata.*field.member = juce::String("value-") + field.key;
        return metadata;
    };

    auto projectObjectOf = [](const juce::var& json) {
        auto* root = json.getDynamicObject();
        REQUIRE(root != nullptr);
        auto* project = root->getProperty("project").getDynamicObject();
        REQUIRE(project != nullptr);
        return project;
    };

    SECTION("every DAWproject metadata field survives a save and load") {
        ProjectInfo info;
        info.name = "Credits";
        info.metadata = filled();

        ProjectInfo loaded;
        REQUIRE(ProjectSerializer::deserializeProject(ProjectSerializer::serializeProject(info),
                                                      loaded));

        for (const auto& field : kProjectMetadataFields) {
            INFO(field.key);
            REQUIRE(loaded.metadata.*field.member == juce::String("value-") + field.key);
        }
    }

    SECTION("an uncredited project writes no metadata object at all") {
        ProjectInfo info;
        info.name = "Uncredited";
        REQUIRE(info.metadata.isEmpty());

        // The var owns the DynamicObject the pointers below run through, so it
        // has to outlive them.
        const auto json = ProjectSerializer::serializeProject(info);
        REQUIRE_FALSE(projectObjectOf(json)->hasProperty("metadata"));
    }

    SECTION("only the fields that were filled in are written") {
        ProjectInfo info;
        info.metadata.artist = "Solo";

        const auto json = ProjectSerializer::serializeProject(info);
        const auto metadata = projectObjectOf(json)->getProperty("metadata");
        auto* metadataObj = metadata.getDynamicObject();
        REQUIRE(metadataObj != nullptr);
        REQUIRE(metadataObj->getProperties().size() == 1);
        REQUIRE(metadataObj->getProperty("artist").toString() == "Solo");
    }

    SECTION("a project saved before the block existed loads with empty credits") {
        ProjectInfo info;
        info.name = "Old Project";
        auto json = ProjectSerializer::serializeProject(info);

        // Loading into a struct that already holds someone else's credits has to
        // clear them, or the previous project's artist shows under this one's
        // name for as long as the window stays open.
        ProjectInfo loaded;
        loaded.metadata = filled();
        REQUIRE(ProjectSerializer::deserializeProject(json, loaded));
        REQUIRE(loaded.metadata.isEmpty());
    }
}

TEST_CASE("A new project is seeded with the stored credit defaults",
          "[project][manager][metadata]") {
    ProjectTestFixture fixture;

    auto& config = Config::getInstance();
    const auto restore = config.getProjectMetadataDefaults();

    std::map<std::string, std::string> defaults;
    for (const auto& field : kProjectMetadataFields)
        defaults[field.key] = std::string("default-") + field.key;
    config.setProjectMetadataDefaults(defaults);

    REQUIRE(ProjectManager::getInstance().newProject());
    const auto& metadata = ProjectManager::getInstance().getCurrentProjectInfo().metadata;

    for (const auto& field : kProjectMetadataFields) {
        INFO(field.key);
        if (field.seededFromDefaults) {
            // Describes the person, so it carries over.
            REQUIRE(metadata.*field.member == juce::String("default-") + field.key);
        } else {
            // Describes the work. A stored value for the title or the year would
            // be wrong in every project after the first, so it is ignored even
            // when the config file has one.
            REQUIRE((metadata.*field.member).isEmpty());
        }
    }

    config.setProjectMetadataDefaults(restore);
}

TEST_CASE("DeviceInfo pluginState roundtrip", "[project][serialization][pluginState]") {
    ProjectTestFixture fixture;

    SECTION("pluginState is serialized and deserialized") {
        auto& trackManager = TrackManager::getInstance();

        auto trackId = trackManager.createTrack("Plugin State Track", TrackType::Media);

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

        auto trackId = trackManager.createTrack("No State Track", TrackType::Media);

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
    auto trackId = trackManager.createTrack("Session Track", TrackType::Media);
    auto* track = trackManager.getTrack(trackId);
    REQUIRE(track != nullptr);
    track->volume = 0.25f;
    track->pan = -0.5f;
    track->manualVolume = 0.75f;
    track->manualPan = 0.25f;
    track->playbackMode = TrackPlaybackMode::Session;
    track->mixerChannelWidth = 137;
    track->mixerFaderTopInset = 42;

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
    REQUIRE(trackObj->hasProperty("mixerChannelWidth"));
    REQUIRE(trackObj->hasProperty("mixerFaderTopInset"));

    ProjectInfo loadedInfo;
    REQUIRE(ProjectSerializer::deserializeProject(json, loadedInfo));
    auto* loaded = trackManager.getTrack(trackId);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->volume == Approx(0.25f));
    REQUIRE(loaded->pan == Approx(-0.5f));
    REQUIRE(loaded->manualVolume == Approx(0.75f));
    REQUIRE(loaded->manualPan == Approx(0.25f));
    REQUIRE(loaded->playbackMode == TrackPlaybackMode::Session);
    REQUIRE(loaded->mixerChannelWidth == 137);
    REQUIRE(loaded->mixerFaderTopInset == 42);
}

TEST_CASE("TrackInfo mixer layout commands undo redo",
          "[project][serialization][track][mixer][undo]") {
    ProjectTestFixture fixture;

    auto& trackManager = TrackManager::getInstance();
    auto& undoManager = UndoManager::getInstance();
    auto trackId = trackManager.createTrack("Resizable", TrackType::Media);
    auto* track = trackManager.getTrack(trackId);
    REQUIRE(track != nullptr);

    undoManager.executeCommand(std::make_unique<SetTrackMixerChannelWidthCommand>(trackId, 128));
    REQUIRE(track->mixerChannelWidth == 128);
    REQUIRE(undoManager.undo());
    REQUIRE(track->mixerChannelWidth == 0);
    REQUIRE(undoManager.redo());
    REQUIRE(track->mixerChannelWidth == 128);

    undoManager.executeCommand(std::make_unique<SetTrackMixerFaderTopInsetCommand>(trackId, 55));
    REQUIRE(track->mixerFaderTopInset == 55);
    REQUIRE(undoManager.undo());
    REQUIRE(track->mixerFaderTopInset == 0);
    REQUIRE(undoManager.redo());
    REQUIRE(track->mixerFaderTopInset == 55);

    undoManager.clearHistory();
    {
        CompoundOperationScope scope("Reset Mixer Channel Width");
        undoManager.executeCommand(
            std::make_unique<SetTrackMixerChannelWidthCommand>(trackId, 128, 0));
    }
    REQUIRE(track->mixerChannelWidth == 0);
    REQUIRE(undoManager.undo());
    REQUIRE(track->mixerChannelWidth == 128);

    {
        CompoundOperationScope scope("Reset Mixer Fader Height");
        undoManager.executeCommand(
            std::make_unique<SetTrackMixerFaderTopInsetCommand>(trackId, 55, 0));
    }
    REQUIRE(track->mixerFaderTopInset == 0);
    REQUIRE(undoManager.undo());
    REQUIRE(track->mixerFaderTopInset == 55);

    auto* master = trackManager.getTrack(MASTER_TRACK_ID);
    REQUIRE(master != nullptr);
    undoManager.clearHistory();

    undoManager.executeCommand(
        std::make_unique<SetTrackMixerChannelWidthCommand>(MASTER_TRACK_ID, 142));
    REQUIRE(master->mixerChannelWidth == 142);
    REQUIRE(undoManager.undo());
    REQUIRE(master->mixerChannelWidth == 0);

    undoManager.executeCommand(
        std::make_unique<SetTrackMixerFaderTopInsetCommand>(MASTER_TRACK_ID, 64));
    REQUIRE(master->mixerFaderTopInset == 64);
    REQUIRE(undoManager.undo());
    REQUIRE(master->mixerFaderTopInset == 0);
}

TEST_CASE("DeviceInfo panel UI state roundtrip", "[project][serialization][device]") {
    ProjectTestFixture fixture;

    auto& trackManager = TrackManager::getInstance();
    auto trackId = trackManager.createTrack("Device Track", TrackType::Media);

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

TEST_CASE("Device channel counts survive a project roundtrip", "[project][serialization][device]") {
    ProjectTestFixture fixture;

    auto& trackManager = TrackManager::getInstance();
    auto trackId = trackManager.createTrack("Channel Counts", TrackType::Media);

    DeviceInfo mono;
    mono.id = 31;
    mono.name = "Mono Effect";
    mono.pluginId = "internal.mono";
    mono.format = PluginFormat::Internal;
    mono.audioInputChannels = 1;
    mono.audioOutputChannels = 1;
    REQUIRE(trackManager.addDeviceToTrack(trackId, mono) != INVALID_DEVICE_ID);

    DeviceInfo stereo;
    stereo.id = 32;
    stereo.name = "Stereo Effect";
    stereo.pluginId = "internal.stereo";
    stereo.format = PluginFormat::Internal;
    REQUIRE(trackManager.addDeviceToTrack(trackId, stereo) != INVALID_DEVICE_ID);

    ProjectInfo info;
    auto json = ProjectSerializer::serializeProject(info);

    ProjectInfo loadedInfo;
    REQUIRE(ProjectSerializer::deserializeProject(json, loadedInfo));
    auto* loadedTrack = trackManager.getTrack(trackId);
    REQUIRE(loadedTrack != nullptr);
    REQUIRE(loadedTrack->chain.fxChainElements.size() == 2);

    const auto& loadedMono = getDevice(loadedTrack->chain.fxChainElements[0]);
    CHECK(loadedMono.audioInputChannels == 1);
    CHECK(loadedMono.audioOutputChannels == 1);

    const auto& loadedStereo = getDevice(loadedTrack->chain.fxChainElements[1]);
    CHECK(loadedStereo.audioInputChannels == 2);
    CHECK(loadedStereo.audioOutputChannels == 2);
}

TEST_CASE("A zero output count is never written to a project", "[project][serialization][device]") {
    // The one count that can silence the chain behind a device. It has to come
    // from a plugin that is really there: on a machine where the plugin is
    // missing it would starve a chain the current engine would have passed
    // straight through.
    DeviceInfo silentDevice;
    silentDevice.id = 34;
    silentDevice.name = "MIDI Only";
    silentDevice.pluginId = "internal.midionly";
    silentDevice.format = PluginFormat::Internal;
    silentDevice.audioInputChannels = 1;
    silentDevice.audioOutputChannels = 0;

    const auto json = ProjectSerializer::serializeDeviceInfo(silentDevice);
    REQUIRE(json.isObject());
    CHECK(json.getDynamicObject()->hasProperty("audioInputChannels"));
    CHECK_FALSE(json.getDynamicObject()->hasProperty("audioOutputChannels"));

    DeviceInfo loaded;
    REQUIRE(ProjectSerializer::deserializeDeviceInfo(json, loaded));
    CHECK(loaded.audioInputChannels == 1);
    CHECK(loaded.audioOutputChannels == 2);
}

TEST_CASE("A zero output count already on disk is ignored", "[project][serialization][device]") {
    // Checked on the way in too, so a hand-edited or older project cannot
    // silence a chain either.
    DeviceInfo device;
    device.id = 35;
    device.name = "Hand Edited";
    device.pluginId = "internal.handedited";
    device.format = PluginFormat::Internal;

    auto json = ProjectSerializer::serializeDeviceInfo(device);
    REQUIRE(json.isObject());
    json.getDynamicObject()->setProperty("audioOutputChannels", 0);

    DeviceInfo loaded;
    REQUIRE(ProjectSerializer::deserializeDeviceInfo(json, loaded));
    CHECK(loaded.audioOutputChannels == 2);
}

TEST_CASE("A device with no channel counts written loads as stereo",
          "[project][serialization][device]") {
    // What a project saved before the counts existed looks like. Stereo is the
    // default and also what the current engine falls back to when it cannot ask
    // the plugin, so an old project keeps the graph it had.
    DeviceInfo device;
    device.id = 33;
    device.name = "Legacy";
    device.pluginId = "internal.legacy";
    device.format = PluginFormat::Internal;

    const auto json = ProjectSerializer::serializeDeviceInfo(device);
    REQUIRE(json.isObject());
    CHECK_FALSE(json.getDynamicObject()->hasProperty("audioInputChannels"));
    CHECK_FALSE(json.getDynamicObject()->hasProperty("audioOutputChannels"));

    DeviceInfo loaded;
    REQUIRE(ProjectSerializer::deserializeDeviceInfo(json, loaded));
    CHECK(loaded.audioInputChannels == 2);
    CHECK(loaded.audioOutputChannels == 2);
}

TEST_CASE("Section-scoped device ids survive project roundtrip",
          "[project][serialization][devices]") {
    ProjectTestFixture fixture;

    auto& trackManager = TrackManager::getInstance();
    auto trackId = trackManager.createTrack("Section IDs", TrackType::Media);

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

TEST_CASE("Post-FX parameter selections survive project roundtrip",
          "[project][serialization][devices]") {
    ProjectTestFixture fixture;

    auto& trackManager = TrackManager::getInstance();
    auto trackId = trackManager.createTrack("Post FX Params", TrackType::Media);

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
    trackManager.setDeviceAiSoundDesignerParameters(fxPath, {8, 9});
    trackManager.setDeviceAiSoundDesignerPrompt(fxPath, "Keep the low end mono.");
    trackManager.setDeviceVisibleParameters(postPath, {4, 5});
    trackManager.setDeviceMiniMixerParameters(postPath, {6, 7});
    trackManager.setDeviceAiSoundDesignerParameters(postPath, {10, 11});
    trackManager.setDeviceAiSoundDesignerPrompt(postPath, "Prefer subtle modulation.");

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
    REQUIRE(loadedFx->aiSoundDesignerParameters == std::vector<int>{8, 9});
    REQUIRE(loadedFx->aiSoundDesignerPrompt == "Keep the low end mono.");
    REQUIRE(loadedPost->visibleParameters == std::vector<int>{4, 5});
    REQUIRE(loadedPost->miniMixerParameters == std::vector<int>{6, 7});
    REQUIRE(loadedPost->aiSoundDesignerParameters == std::vector<int>{10, 11});
    REQUIRE(loadedPost->aiSoundDesignerPrompt == "Prefer subtle modulation.");
}

TEST_CASE("Mixer analysis plugin state survives project roundtrip",
          "[project][serialization][devices][pluginState]") {
    ProjectTestFixture fixture;

    auto& trackManager = TrackManager::getInstance();
    auto& projectManager = ProjectManager::getInstance();
    auto firstTrackId = trackManager.createTrack("First", TrackType::Media);
    auto secondTrackId = trackManager.createTrack("Second", TrackType::Media);

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

    auto childTrackId = trackManager.createTrack("Later Track", TrackType::Media);
    DeviceInfo siblingScope = scope;
    siblingScope.pluginState = "<PLUGIN type=\"oscilloscope\" traceColour=\"6\"/>";
    REQUIRE(trackManager.addDeviceToMixerAnalysis(childTrackId, siblingScope) == 2);
}

TEST_CASE("Post-fx device params are not automation targets",
          "[project][serialization][automation]") {
    ProjectTestFixture fixture;

    auto& trackManager = TrackManager::getInstance();
    auto& automationManager = AutomationManager::getInstance();
    auto trackId = trackManager.createTrack("Automation Section IDs", TrackType::Media);

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
    auto trackId = trackManager.createTrack("Parameter Track", TrackType::Media);

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
    param.momentary = true;
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
    REQUIRE(paramObj->hasProperty("momentary"));

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
    REQUIRE(loaded.momentary);
    REQUIRE_FALSE(static_cast<bool>(loaded.displayText));
}

TEST_CASE("MIDI controller curve metadata roundtrip", "[project][serialization][midi]") {
    ProjectTestFixture fixture;

    auto trackId = TrackManager::getInstance().createTrack("MIDI", TrackType::Media);

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

TEST_CASE("Delta solo state roundtrips and defaults off for older projects",
          "[project][serialization][delta_solo]") {
    DeviceInfo device;
    device.id = 11;
    device.name = "Delta Effect";
    device.deltaSolo = true;

    auto deviceJson = ProjectSerializer::serializeDeviceInfo(device);
    DeviceInfo loadedDevice;
    REQUIRE(ProjectSerializer::deserializeDeviceInfo(deviceJson, loadedDevice));
    CHECK(loadedDevice.deltaSolo);

    deviceJson.getDynamicObject()->removeProperty("deltaSolo");
    DeviceInfo legacyDevice;
    legacyDevice.deltaSolo = false;
    REQUIRE(ProjectSerializer::deserializeDeviceInfo(deviceJson, legacyDevice));
    CHECK_FALSE(legacyDevice.deltaSolo);

    RackInfo rack;
    rack.id = 12;
    rack.name = "Delta Rack";
    rack.deltaSolo = true;

    auto rackJson = ProjectSerializer::serializeRackInfo(rack);
    RackInfo loadedRack;
    REQUIRE(ProjectSerializer::deserializeRackInfo(rackJson, loadedRack));
    CHECK(loadedRack.deltaSolo);

    rackJson.getDynamicObject()->removeProperty("deltaSolo");
    RackInfo legacyRack;
    legacyRack.deltaSolo = false;
    REQUIRE(ProjectSerializer::deserializeRackInfo(rackJson, legacyRack));
    CHECK_FALSE(legacyRack.deltaSolo);
}

TEST_CASE("Saved automation targets keep the flag that makes a track path valid",
          "[serialization][automation]") {
    // The project writer previously omitted isTrackLevel, so every saved
    // TrackVolume/TrackPan/SendLevel target reloaded as a path carrying a track
    // id but addressing no node.
    auto& tracks = TrackManager::getInstance();
    auto& automation = AutomationManager::getInstance();
    automation.clearAll();
    tracks.clearAllTracks();

    const auto trackId = tracks.createTrack("Bass", TrackType::Media);

    AutomationTarget target;
    target.kind = ControlTarget::Kind::TrackVolume;
    target.devicePath = ChainNodePath::trackLevel(trackId);
    REQUIRE(automation.createLane(target, AutomationLaneType::Absolute) !=
            INVALID_AUTOMATION_LANE_ID);

    const auto json = ProjectSerializer::serializeAutomation();
    const auto* lanes = json["lanes"].getArray();
    REQUIRE(lanes != nullptr);
    REQUIRE(lanes->size() == 1);

    const auto devicePath = lanes->getReference(0)["target"]["devicePath"];
    REQUIRE(devicePath.isObject());
    CHECK(static_cast<bool>(devicePath["isTrackLevel"]));

    // What was written must parse back to the path that was saved.
    ChainNodePath reloaded;
    REQUIRE(fromVar(devicePath, reloaded));
    CHECK(reloaded == ChainNodePath::trackLevel(trackId));
    CHECK(reloaded.isValid());

    automation.clearAll();
    tracks.clearAllTracks();
}

TEST_CASE("A hardware insert's send and return roundtrip", "[project][serialization][insert]") {
    // What an insert is has to survive a save (#2245). It used to live only
    // inside a te::InsertPlugin's ValueTree, so the only thing that knew what an
    // insert sent to was the fork; the native engine compiles a send op and a
    // return op from the model, and the model is what a project writes down.
    ProjectTestFixture fixture;

    auto trackId = TrackManager::getInstance().createTrack("Outboard", TrackType::Media);

    DeviceInfo device;
    device.id = 1;
    device.name = "External FX";
    device.pluginId = "insert";
    device.deviceType = DeviceType::Effect;
    device.insert.sendType = InsertConfig::Endpoint::Audio;
    device.insert.returnType = InsertConfig::Endpoint::Audio;
    device.insert.sendDevice = "Out 3-4";
    device.insert.returnDevice = "In 3-4";

    // Negative on purpose. An interface reports its own buffering and knows
    // nothing about the converter and the cable past it, so a person with a
    // loopback corrects in both directions and a field that clamped would lose
    // half of what they measured.
    device.insert.manualAdjustMs = -2.5;

    TrackManager::getInstance().addDeviceToTrack(trackId, device);

    ProjectInfo info;
    info.name = "Inserts";
    info.tempo = 120.0;

    auto json = ProjectSerializer::serializeProject(info);

    ProjectInfo loaded;
    REQUIRE(ProjectSerializer::deserializeProject(json, loaded));

    const auto* track = TrackManager::getInstance().getTrack(trackId);
    REQUIRE(track != nullptr);
    REQUIRE(track->chain.fxChainElements.size() == 1);
    REQUIRE(magda::isDevice(track->chain.fxChainElements.front()));

    const auto& restored = magda::getDevice(track->chain.fxChainElements.front()).insert;
    CHECK(restored.sendType == InsertConfig::Endpoint::Audio);
    CHECK(restored.returnType == InsertConfig::Endpoint::Audio);
    CHECK(restored.sendDevice == "Out 3-4");
    CHECK(restored.returnDevice == "In 3-4");
    CHECK(restored.manualAdjustMs == Approx(-2.5));
    CHECK(restored.isActive());
}

TEST_CASE("A device that is not an insert writes no insert block",
          "[project][serialization][insert]") {
    // The mirror, and it is what keeps the field from costing every project in
    // the corpus a block it has no use for: an insert is the only device that
    // has one, and isActive() is what says so without anything having to know a
    // plugin id.
    ProjectTestFixture fixture;

    auto trackId = TrackManager::getInstance().createTrack("Ordinary", TrackType::Media);

    DeviceInfo device;
    device.id = 1;
    device.name = "Reverb";
    device.deviceType = DeviceType::Effect;
    TrackManager::getInstance().addDeviceToTrack(trackId, device);

    ProjectInfo info;
    info.name = "No Inserts";
    info.tempo = 120.0;

    auto json = ProjectSerializer::serializeProject(info);
    auto* rootObj = json.getDynamicObject();
    REQUIRE(rootObj != nullptr);

    auto* tracks = rootObj->getProperty("tracks").getArray();
    REQUIRE(tracks != nullptr);
    REQUIRE_FALSE(tracks->isEmpty());

    auto* trackObj = tracks->getReference(0).getDynamicObject();
    REQUIRE(trackObj != nullptr);
    auto* elements = trackObj->getProperty("chainElements").getArray();
    REQUIRE(elements != nullptr);
    REQUIRE_FALSE(elements->isEmpty());

    auto* deviceObj =
        elements->getReference(0).getDynamicObject()->getProperty("device").getDynamicObject();
    REQUIRE(deviceObj != nullptr);
    CHECK_FALSE(deviceObj->hasProperty("insert"));

    // And a project with no block loads as a device that is not an insert,
    // which is every project saved before this existed.
    ProjectInfo loaded;
    REQUIRE(ProjectSerializer::deserializeProject(json, loaded));

    const auto* track = TrackManager::getInstance().getTrack(trackId);
    REQUIRE(track != nullptr);
    REQUIRE(track->chain.fxChainElements.size() == 1);
    CHECK_FALSE(magda::getDevice(track->chain.fxChainElements.front()).insert.isActive());
}
