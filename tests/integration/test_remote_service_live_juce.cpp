// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <thread>
#include <vector>

#include "RemoteTestScopes.hpp"
#include "magda/agents/agent_tool_bridge.hpp"
#include "magda/daw/api/magda_api_live.hpp"
#include "magda/daw/api/remote_engine_jobs.hpp"
#include "magda/daw/api/remote_model_bridge.hpp"
#include "magda/daw/api/remote_service.hpp"
#include "magda/daw/api/remote_speech_to_text.hpp"
#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TempoMap.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"
#include "magda/daw/project/ProjectManager.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"

// Remote API driven against the live facade and the real singletons, so these
// assert on what actually happens to the project rather than on what a mock
// recorded. They live in the JUCE target because executing an undoable command
// constructs ProjectManager through UndoableMutationScope, whose constructor
// starts a timer — that needs an initialised message system to be valid, which
// the Catch2 runner does not have.

namespace {

using namespace magda;
using namespace magda::remote;
using magda::test::fullyGrantedContext;

juce::var object(std::initializer_list<std::pair<const char*, juce::var>> fields) {
    auto* result = new juce::DynamicObject();
    for (const auto& [key, value] : fields)
        result->setProperty(key, value);
    return result;
}

MidiNote makeMidiNote(int pitch, double startBeat, double lengthBeats) {
    MidiNote note;
    note.noteNumber = pitch;
    note.velocity = 100;
    note.startBeat = startBeat;
    note.lengthBeats = lengthBeats;
    return note;
}

juce::var arrangementDestination(TrackId trackId, double startBeat) {
    return object({{"view", "arrangement"},
                   {"trackId", static_cast<int>(trackId)},
                   {"startBeat", startBeat}});
}

juce::var sessionDestination(TrackId trackId, SceneId sceneId,
                             const char* occupiedPolicy = "fail") {
    return object({{"view", "session"},
                   {"trackId", static_cast<int>(trackId)},
                   {"sceneId", static_cast<int>(sceneId)},
                   {"occupiedPolicy", occupiedPolicy}});
}

class FlatTempoMap final : public TempoMap {
  public:
    explicit FlatTempoMap(double bpm) : bpm_(bpm) {}
    double beatToTime(double beat) const override {
        return beat * 60.0 / bpm_;
    }
    double timeToBeat(double seconds) const override {
        return seconds * bpm_ / 60.0;
    }
    double bpmAt(double) const override {
        return bpm_;
    }

  private:
    double bpm_;
};

/// A mono WAV of @p seconds of quiet noise at 48 kHz, written for the test.
juce::File writeSample(const juce::File& file, double seconds) {
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(new juce::FileOutputStream(file), 48000.0, 1, 16, {}, 0));
    juce::AudioBuffer<float> buffer(1, static_cast<int>(seconds * 48000.0));
    juce::Random random(7);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample(0, i, random.nextFloat() * 0.1f);
    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    return file;
}

/// One second of silence, a second of a 220 Hz voice, then a second of silence.
juce::File writeVoice(const juce::File& file) {
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(new juce::FileOutputStream(file), 48000.0, 1, 24, {}, 0));
    juce::AudioBuffer<float> buffer(1, 3 * 48000);
    buffer.clear();
    for (int i = 48000; i < 2 * 48000; ++i) {
        const double phase = 2.0 * juce::MathConstants<double>::pi * 220.0 * i / 48000.0;
        double value = 0.0;
        for (int h = 1; h <= 20; ++h)
            value += std::sin(h * phase) / h;
        buffer.setSample(0, i, static_cast<float>(0.1 * value));
    }
    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    return file;
}

/// Renders by writing a sample of the range's length, at once, so a render job finishes inline.
class SampleWritingEngineJobs final : public EngineJobSource {
  public:
    EngineJobStartStatus renderRange(OfflineRenderRequest request,
                                     std::shared_ptr<std::atomic_bool>, ProgressCallback,
                                     CompletionCallback onComplete) override {
        lastRequest = request;
        writeSample(request.destination,
                    (request.range.end.value - request.range.start.value) * 60.0 / 120.0);
        if (onComplete)
            onComplete({EngineJobResultStatus::Succeeded});
        return EngineJobStartStatus::Started;
    }
    EngineJobStartStatus freezeTrack(TrackId, std::shared_ptr<std::atomic_bool>, ProgressCallback,
                                     CompletionCallback, juce::String&) override {
        return EngineJobStartStatus::Unavailable;
    }
    EngineJobStartStatus startMasterCapture(const juce::String&, const juce::String&,
                                            const MasterCaptureRequest&) override {
        return EngineJobStartStatus::Unavailable;
    }
    MasterCaptureResult stopMasterCapture(const juce::String&) override {
        return {false, "unsupported"};
    }
    void cancelMasterCapture(const juce::String&) override {}
    MasterCaptureSnapshot masterCaptureStatus() const override {
        return {};
    }
    EngineJobStartStatus runFileTask(std::function<void()> work, std::shared_ptr<std::atomic_bool>,
                                     std::function<void()> onComplete) override {
        if (deferFileTasks) {
            pendingFileTask = [work = std::move(work), onComplete = std::move(onComplete)] {
                work();
                onComplete();
            };
            return EngineJobStartStatus::Started;
        }
        work();
        onComplete();
        return EngineJobStartStatus::Started;
    }
    void shutdown() override {}

    OfflineRenderRequest lastRequest;
    /// Holds a file task until the test runs it, as a long analysis keeps its job running.
    bool deferFileTasks = false;
    std::function<void()> pendingFileTask;
};

SceneId sceneIdAt(int index) {
    return ProjectManager::getInstance()
        .getCurrentProjectInfo()
        .scenes.at(static_cast<std::size_t>(index))
        .id;
}

class RemoteServiceLiveTest final : public juce::UnitTest {
  public:
    RemoteServiceLiveTest() : juce::UnitTest("Remote Service Live", "magda") {}

    void runTest() override {
        beginTest("Project new and close are dialog-free lifecycle boundaries");
        {
            Fixture fixture;
            auto& projects = ProjectManager::getInstance();
            expect(projects.newProject(ProjectManager::UnsavedChangesPolicy::Discard));
            expect(projects.hasOpenProject());

            const auto track =
                fixture.run("tracks.create", object({{"name", "Outgoing"}, {"type", "audio"}}));
            expect(track.ok);
            expect(UndoManager::getInstance().canUndo());
            projects.markDirty();

            const auto before = fixture.service.currentRevision();
            const auto refused = fixture.run("project.close", object({}));
            expect(!refused.ok);
            expect(refused.error.code == ErrorCode::Conflict);
            expect(projects.hasOpenProject());
            expectEquals(static_cast<juce::int64>(fixture.service.currentRevision()),
                         static_cast<juce::int64>(before));

            const auto closed =
                fixture.run("project.close", object({{"discardUnsavedChanges", true}}));
            expect(closed.ok);
            expect(!projects.hasOpenProject());
            expect(!static_cast<bool>(closed.result["open"]));
            expect(!UndoManager::getInstance().canUndo());
            expect(TrackManager::getInstance().getTracks().empty());
            expectEquals(static_cast<juce::int64>(closed.revision),
                         static_cast<juce::int64>(before + 1));

            const auto noOp = fixture.run("project.close", object({}));
            expect(noOp.ok);
            expectEquals(static_cast<juce::int64>(noOp.revision),
                         static_cast<juce::int64>(closed.revision));

            const auto created = fixture.run("project.new", object({}));
            expect(created.ok);
            expect(projects.hasOpenProject());
            expect(static_cast<bool>(created.result["open"]));
            expectEquals(static_cast<juce::int64>(created.revision),
                         static_cast<juce::int64>(closed.revision + 1));
            expect(!UndoManager::getInstance().canUndo());

            const auto nextTrack =
                fixture.run("tracks.create", object({{"name", "Second"}, {"type", "audio"}}));
            expect(nextTrack.ok);
            projects.markDirty();
            const auto newRefused = fixture.run("project.new", object({}));
            expect(!newRefused.ok);
            expect(newRefused.error.code == ErrorCode::Conflict);
            expect(TrackManager::getInstance().getTracks().size() == 1);

            const auto replaced =
                fixture.run("project.new", object({{"discardUnsavedChanges", true}}));
            expect(replaced.ok);
            expect(projects.hasOpenProject());
            expect(TrackManager::getInstance().getTracks().empty());
            expect(!UndoManager::getInstance().canUndo());
            expectEquals(static_cast<juce::int64>(replaced.revision),
                         static_cast<juce::int64>(nextTrack.revision + 1));
        }

        beginTest("Project open commits one remote lifecycle boundary");
        {
            Fixture fixture;
            auto& projects = ProjectManager::getInstance();
            expect(projects.newProject(ProjectManager::UnsavedChangesPolicy::Discard));

            const auto source = juce::File::createTempFile(".mgd");
            ProjectInfo saved;
            saved.name = "Remote Open";
            saved.tempo = 137.0;
            expect(ProjectSerializer::saveToFile(source, saved));

            TrackManager::getInstance().createTrack("Outgoing", TrackType::Media);
            UndoManager::getInstance().clearHistory();
            const auto before = fixture.service.currentRevision();
            auto request = fullyGrantedContext();
            request.clientId = "live-file-client";
            request.clientName = "live-file-client";
            const auto accepted = fixture.run("project.open",
                                              object({{"path", source.getFullPathName()},
                                                      {"dirtyPolicy", "discard"},
                                                      {"autosavePolicy", "ignore"},
                                                      {"missingMediaPolicy", "fail"},
                                                      {"unavailableDevicePolicy", "fail"}}),
                                              request);
            expect(accepted.ok);

            const auto completed = fixture.waitForJob(accepted.result["id"].toString(), request);
            expect(completed.has_value());
            if (completed) {
                expect(completed->state == RemoteJobState::Completed);
                expect(completed->completionRevision == before + 1);
            }
            expectEquals(projects.getCurrentProjectInfo().name, juce::String("Remote Open"));
            expectWithinAbsoluteError(projects.getCurrentProjectInfo().tempo, 137.0, 0.001);
            expect(TrackManager::getInstance().getTracks().empty());
            expect(!UndoManager::getInstance().canUndo());
            expect(fixture.service.currentRevision() == before + 1);
            expect(!projects.interactiveRecoveryAllowedForCurrentOpen());

            expect(projects.newProject(ProjectManager::UnsavedChangesPolicy::Discard));
            expect(projects.interactiveRecoveryAllowedForCurrentOpen());
            source.deleteFile();
        }

        beginTest(
            "An opened project's loop reaches the engine, or the UI's restorer when it has one");
        {
            Fixture fixture;
            auto& projects = ProjectManager::getInstance();
            expect(projects.newProject(ProjectManager::UnsavedChangesPolicy::Discard));

            const auto source = juce::File::createTempFile(".mgd");
            ProjectInfo saved;
            saved.name = "Looped";
            saved.loopEnabled = true;
            saved.loopStartBeats = 0.0;
            saved.loopEndBeats = 32.0;
            expect(ProjectSerializer::saveToFile(source, saved));

            auto request = fullyGrantedContext();
            request.clientId = "loop-open-client";
            request.clientName = "loop-open-client";
            const auto open = [&] {
                const auto accepted = fixture.run("project.open",
                                                  object({{"path", source.getFullPathName()},
                                                          {"dirtyPolicy", "discard"},
                                                          {"autosavePolicy", "ignore"},
                                                          {"missingMediaPolicy", "fail"},
                                                          {"unavailableDevicePolicy", "fail"}}),
                                                  request);
                expect(accepted.ok);
                const auto job = fixture.waitForJob(accepted.result["id"].toString(), request);
                expect(job && job->state == RemoteJobState::Completed);
                if (job && job->error)
                    logMessage("open failed: " + job->error->message);
            };

            std::optional<bool> engineLoop;
            fixture.api.setProjectLoopEnabledWriter([&](bool enabled) { engineLoop = enabled; });
            open();
            expect(engineLoop == std::optional<bool>(true));

            std::optional<ProjectInfo> restored;
            engineLoop.reset();
            fixture.api.setProjectRestorer([&](const ProjectInfo& info) { restored = info; });
            open();
            expect(restored.has_value() && restored->loopEnabled);
            expectWithinAbsoluteError(restored ? restored->loopEndBeats : 0.0, 32.0, 0.001);
            expect(!engineLoop.has_value());

            restored.reset();
            expect(fixture.run("project.new", object({{"discardUnsavedChanges", true}})).ok);
            expect(restored.has_value() && !restored->loopEnabled);

            fixture.api.setProjectRestorer(nullptr);
            source.deleteFile();
        }

        beginTest("A project holding an External Insert opens when devices must be available");
        {
            // The insert is listed by its variants' ids, never its own (#3164).
            Fixture fixture;
            auto& projects = ProjectManager::getInstance();
            expect(projects.newProject(ProjectManager::UnsavedChangesPolicy::Discard));
            const auto trackId = TrackManager::getInstance().createTrack("Synth", TrackType::Media);
            DeviceInfo insert;
            insert.name = "External Instrument";
            insert.pluginId = "insert";
            insert.format = PluginFormat::Internal;
            insert.isInstrument = true;
            insert.deviceType = DeviceType::Instrument;
            TrackManager::getInstance().addDeviceToTrack(trackId, insert);

            const auto source = juce::File::createTempFile(".mgd");
            ProjectInfo saved;
            saved.name = "Outboard";
            expect(ProjectSerializer::saveToFile(source, saved));
            expect(projects.newProject(ProjectManager::UnsavedChangesPolicy::Discard));

            auto request = fullyGrantedContext();
            request.clientId = "live-file-client";
            request.clientName = "live-file-client";
            const auto accepted = fixture.run("project.open",
                                              object({{"path", source.getFullPathName()},
                                                      {"dirtyPolicy", "discard"},
                                                      {"autosavePolicy", "ignore"},
                                                      {"missingMediaPolicy", "fail"},
                                                      {"unavailableDevicePolicy", "fail"}}),
                                              request);
            expect(accepted.ok);
            const auto completed = fixture.waitForJob(accepted.result["id"].toString(), request);
            expect(completed.has_value() && completed->state == RemoteJobState::Completed);
            expectEquals(projects.getCurrentProjectInfo().name, juce::String("Outboard"));

            expect(projects.newProject(ProjectManager::UnsavedChangesPolicy::Discard));
            source.deleteFile();
        }

        beginTest("A failed project open leaves the active project untouched");
        {
            Fixture fixture;
            auto& projects = ProjectManager::getInstance();
            expect(projects.newProject(ProjectManager::UnsavedChangesPolicy::Discard));
            const auto trackId = TrackManager::getInstance().createTrack("Keep", TrackType::Media);
            const auto originalName = projects.getCurrentProjectInfo().name;
            const auto before = fixture.service.currentRevision();

            const auto source = juce::File::createTempFile(".mgd");
            expect(source.replaceWithText("not a project"));
            auto request = fullyGrantedContext();
            request.clientId = "live-file-client";
            request.clientName = "live-file-client";
            const auto accepted = fixture.run("project.open",
                                              object({{"path", source.getFullPathName()},
                                                      {"dirtyPolicy", "discard"},
                                                      {"autosavePolicy", "ignore"},
                                                      {"missingMediaPolicy", "fail"},
                                                      {"unavailableDevicePolicy", "fail"}}),
                                              request);
            expect(accepted.ok);

            const auto failed = fixture.waitForJob(accepted.result["id"].toString(), request);
            expect(failed.has_value());
            if (failed)
                expect(failed->state == RemoteJobState::Failed);
            expectEquals(projects.getCurrentProjectInfo().name, originalName);
            expect(TrackManager::getInstance().getTrack(trackId) != nullptr);
            expect(fixture.service.currentRevision() == before);
            source.deleteFile();
        }

        beginTest("Project save-as writes to the resolved absolute destination");
        {
            Fixture fixture;
            auto& projects = ProjectManager::getInstance();
            expect(projects.newProject(ProjectManager::UnsavedChangesPolicy::Discard));
            projects.setTempo(141.0);

            const auto destination = juce::File::createTempFile(".mgd");
            const auto target = ProjectManager::saveTargetFor(destination);
            auto request = fullyGrantedContext();
            request.clientId = "live-file-client";
            request.clientName = "live-file-client";
            const auto before = fixture.service.currentRevision();
            const auto accepted = fixture.run("project.saveAs",
                                              object({{"path", destination.getFullPathName()},
                                                      {"overwritePolicy", "fail"},
                                                      {"mediaPolicy", "copy"}}),
                                              request);
            expect(accepted.ok);
            const auto completed = fixture.waitForJob(accepted.result["id"].toString(), request);
            expect(completed.has_value() && completed->state == RemoteJobState::Completed);
            expect(target.existsAsFile());
            expect(fixture.service.currentRevision() == before);
            if (completed)
                expectEquals(completed->result["project"]["path"].toString(),
                             projects.getCurrentProjectFile().getFullPathName());

            const auto overwrite = fixture.run("project.saveAs",
                                               object({{"path", destination.getFullPathName()},
                                                       {"overwritePolicy", "replace"},
                                                       {"mediaPolicy", "copy"}}),
                                               request);
            expect(overwrite.ok);
            const auto replaced = fixture.waitForJob(overwrite.result["id"].toString(), request);
            expect(replaced.has_value() && replaced->state == RemoteJobState::Completed);

            expect(projects.newProject(ProjectManager::UnsavedChangesPolicy::Discard));
            target.getParentDirectory().deleteRecursively();
        }

        beginTest("Nested rack and chain operations are path-addressed and undoable");
        {
            Fixture fixture;
            const auto createdTrack =
                fixture.run("tracks.create", object({{"name", "Nested"}, {"type", "audio"}}));
            expect(createdTrack.ok);
            const auto trackId = static_cast<TrackId>(static_cast<int>(createdTrack.result["id"]));

            const auto outer =
                fixture.run("racks.create",
                            object({{"trackId", static_cast<int>(trackId)}, {"name", "Outer"}}));
            expect(outer.ok);
            const auto outerId = static_cast<RackId>(static_cast<int>(outer.result["id"]));
            const auto outerPath = ChainNodePath::rack(trackId, outerId);
            const auto* outerRack = TrackManager::getInstance().getRackByPath(outerPath);
            expect(outerRack != nullptr && outerRack->chains.size() == 1);
            if (outerRack == nullptr || outerRack->chains.empty())
                return;

            const auto outerChainPath = outerPath.withChain(outerRack->chains.front().id);
            const auto nested = fixture.run(
                "racks.create", object({{"parentPath", toJson(makeDevicePathDto(outerChainPath))},
                                        {"name", "Nested"}}));
            expect(nested.ok);
            const auto nestedId = static_cast<RackId>(static_cast<int>(nested.result["id"]));
            const auto nestedPath = outerChainPath.withRack(nestedId);
            expect(TrackManager::getInstance().getRackByPath(nestedPath) != nullptr);

            // Creation replays the same materialised node, rather than
            // allocating a different public identity on redo.
            expect(UndoManager::getInstance().undo());
            expect(TrackManager::getInstance().getRackByPath(nestedPath) == nullptr);
            expect(UndoManager::getInstance().redo());
            expect(TrackManager::getInstance().getRackByPath(nestedPath) != nullptr);

            const auto addedChain = fixture.run(
                "chains.create", object({{"rackPath", toJson(makeDevicePathDto(nestedPath))},
                                         {"name", "Parallel"}}));
            expect(addedChain.ok);
            const auto chainId = static_cast<ChainId>(static_cast<int>(addedChain.result["id"]));
            const auto chainPath = nestedPath.withChain(chainId);
            expect(TrackManager::getInstance().getChainByPath(chainPath) != nullptr);
            expect(UndoManager::getInstance().undo());
            expect(TrackManager::getInstance().getChainByPath(chainPath) == nullptr);
            expect(UndoManager::getInstance().redo());
            expect(TrackManager::getInstance().getChainByPath(chainPath) != nullptr);

            const auto rackUpdate = fixture.run(
                "racks.update", object({{"rackPath", toJson(makeDevicePathDto(nestedPath))},
                                        {"bypassed", true},
                                        {"volumeDb", -3.0}}));
            expect(rackUpdate.ok);
            expect(static_cast<bool>(rackUpdate.result["bypassed"]));
            expectWithinAbsoluteError(static_cast<double>(rackUpdate.result["volumeDb"]), -3.0,
                                      1.0e-9);
            expect(rackUpdate.result["nodePath"] == toJson(makeDevicePathDto(nestedPath)));

            const auto beforeNoOp = fixture.service.currentRevision();
            const auto rackNoOp = fixture.run(
                "racks.update", object({{"rackPath", toJson(makeDevicePathDto(nestedPath))},
                                        {"bypassed", true},
                                        {"volumeDb", -3.0}}));
            expect(rackNoOp.ok);
            expect(rackNoOp.revision == beforeNoOp);
            expect(UndoManager::getInstance().undo());
            const auto* undoneRack = TrackManager::getInstance().getRackByPath(nestedPath);
            expect(undoneRack != nullptr);
            if (undoneRack != nullptr) {
                expect(!undoneRack->bypassed);
                expectWithinAbsoluteError(static_cast<double>(undoneRack->volume), 0.0, 1.0e-9);
            }
            expect(UndoManager::getInstance().redo());

            const auto chainUpdate = fixture.run(
                "chains.update", object({{"chainPath", toJson(makeDevicePathDto(chainPath))},
                                         {"name", "Wide"},
                                         {"muted", true},
                                         {"volumeDb", -6.0},
                                         {"pan", 0.25}}));
            expect(chainUpdate.ok);
            expectEquals(chainUpdate.result["name"].toString(), juce::String("Wide"));
            expect(static_cast<bool>(chainUpdate.result["muted"]));
            expect(chainUpdate.result["nodePath"] == toJson(makeDevicePathDto(chainPath)));
            expect(UndoManager::getInstance().undo());
            const auto* undoneChain = TrackManager::getInstance().getChainByPath(chainPath);
            expect(undoneChain != nullptr);
            if (undoneChain != nullptr) {
                expectEquals(undoneChain->name, juce::String("Parallel"));
                expect(!undoneChain->muted);
                expectWithinAbsoluteError(static_cast<double>(undoneChain->volume), 0.0, 1.0e-9);
                expectWithinAbsoluteError(static_cast<double>(undoneChain->pan), 0.0, 1.0e-9);
            }
            expect(UndoManager::getInstance().redo());

            const auto graph =
                fixture.run("devices.list", object({{"trackId", static_cast<int>(trackId)}}));
            expect(graph.ok);
            bool foundNestedPath = false;
            if (const auto* racks = graph.result["racks"].getArray()) {
                for (const auto& rack : *racks)
                    foundNestedPath = foundNestedPath ||
                                      rack["nodePath"] == toJson(makeDevicePathDto(nestedPath));
            }
            expect(foundNestedPath);

            const auto removedChain = fixture.run(
                "chains.remove", object({{"chainPath", toJson(makeDevicePathDto(chainPath))}}));
            expect(removedChain.ok,
                   toString(removedChain.error.code) + ": " + removedChain.error.message);
            expect(TrackManager::getInstance().getChainByPath(chainPath) == nullptr);
            expect(UndoManager::getInstance().undo());
            const auto* restoredChain = TrackManager::getInstance().getChainByPath(chainPath);
            expect(restoredChain != nullptr);
            if (restoredChain != nullptr)
                expectEquals(restoredChain->name, juce::String("Wide"));

            const auto removed = fixture.run(
                "racks.remove", object({{"rackPath", toJson(makeDevicePathDto(nestedPath))}}));
            expect(removed.ok);
            expect(TrackManager::getInstance().getRackByPath(nestedPath) == nullptr);
            expect(UndoManager::getInstance().undo());
            expect(TrackManager::getInstance().getRackByPath(nestedPath) != nullptr);
            expect(TrackManager::getInstance().getChainByPath(chainPath) != nullptr);
        }

        beginTest("Chord track ensure is singleton, idempotent, and readable");
        {
            Fixture fixture;
            const auto absent = fixture.run("chordTrack.get", object({}));
            expect(absent.ok);
            expect(absent.result["track"].isVoid());

            const auto before = fixture.service.currentRevision();
            const auto ensured = fixture.run("chordTrack.ensure", object({}));
            expect(ensured.ok);
            expect(ensured.revision == before + 1);
            const auto trackId =
                static_cast<TrackId>(static_cast<int>(ensured.result["track"]["id"]));
            expect(trackId != INVALID_TRACK_ID);

            auto& tracks = TrackManager::getInstance();
            expect(tracks.getChordTrackId() == trackId);
            expect(tracks.createTrack("Must not duplicate", TrackType::Chord) == trackId);
            expect(std::ranges::count(tracks.getTracks(), TrackType::Chord, &TrackInfo::type) == 1);

            const auto beforeNoOp = fixture.service.currentRevision();
            const auto ensuredAgain = fixture.run("chordTrack.ensure", object({}));
            expect(ensuredAgain.ok);
            expect(ensuredAgain.revision == beforeNoOp);
            expect(static_cast<int>(ensuredAgain.result["track"]["id"]) == trackId);

            const auto duplicate =
                fixture.run("tracks.create", object({{"name", "Another"}, {"type", "chord"}}));
            expect(!duplicate.ok);
            expect(duplicate.error.code == ErrorCode::Conflict);
            expect(std::ranges::count(tracks.getTracks(), TrackType::Chord, &TrackInfo::type) == 1);

            const auto clipId = ClipManager::getInstance().createMidiClipBeats(trackId, 8.0, 8.0);
            ClipManager::getInstance().addChordAnnotation(
                clipId, ClipInfo::ChordAnnotation{2.0, 2.0, "Am7", 123});
            const auto snapshot = fixture.run("chordTrack.get", object({}));
            expect(snapshot.ok);
            const auto* chords = snapshot.result["chords"].getArray();
            expect(chords != nullptr && chords->size() == 1);
            if (chords != nullptr && chords->size() == 1) {
                expect(static_cast<int>((*chords)[0]["clipId"]) == clipId);
                expectWithinAbsoluteError(static_cast<double>((*chords)[0]["clipBeat"]), 2.0,
                                          1.0e-9);
                expectWithinAbsoluteError(static_cast<double>((*chords)[0]["startBeat"]), 10.0,
                                          1.0e-9);
                expectEquals((*chords)[0]["name"].toString(), juce::String("Am7"));
                expect((*chords)[0]["chordGroup"].isVoid());
            }

            expect(UndoManager::getInstance().undo());
            expect(tracks.getChordTrackId() == INVALID_TRACK_ID);
        }

        beginTest("Chord progression replacement is atomic, undoable, and idempotent");
        {
            Fixture fixture;
            auto& tracks = TrackManager::getInstance();
            auto& clips = ClipManager::getInstance();
            juce::Array<juce::var> progression;
            progression.add(object(
                {{"startBeat", 0.0}, {"lengthBeats", 4.0}, {"root", "C"}, {"quality", "maj"}}));
            progression.add(object(
                {{"startBeat", 4.0}, {"lengthBeats", 4.0}, {"root", "G"}, {"quality", "7"}}));
            const auto input = object({{"chords", juce::var(progression)}});

            const auto before = fixture.service.currentRevision();
            const auto created = fixture.run("chordTrack.replaceProgression", input);
            expect(created.ok, created.error.message);
            expect(created.revision == before + 1);
            const auto trackId = tracks.getChordTrackId();
            expect(trackId != INVALID_TRACK_ID);
            const auto* entries = created.result["chords"].getArray();
            expect(entries != nullptr && entries->size() == 2);
            const auto ids = clips.getClipsOnTrack(trackId);
            expect(ids.size() == 1);
            if (ids.size() == 1) {
                const auto* clip = clips.getClip(ids.front());
                expect(clip != nullptr && clip->chordAnnotations.size() == 2);
                expect(clip != nullptr && clip->midiNotes.size() == 7);
                if (clip != nullptr) {
                    expect(clip->chordAnnotations[0].chordGroup == 1);
                    expect(clip->midiNotes[0].chordGroup == 1);
                    expect(clip->midiNotes[3].chordGroup == 2);
                }
            }

            const auto same = fixture.run("chordTrack.replaceProgression", input);
            expect(same.ok);
            expect(same.revision == created.revision);

            juce::Array<juce::var> invalid = progression;
            invalid.add(object(
                {{"startBeat", 3.0}, {"lengthBeats", 2.0}, {"root", "D"}, {"quality", "min"}}));
            const auto rejected = fixture.run("chordTrack.replaceProgression",
                                              object({{"chords", juce::var(invalid)}}));
            expect(!rejected.ok);
            expect(rejected.revision == created.revision);
            expect(clips.getClipsOnTrack(trackId) == ids);

            expect(UndoManager::getInstance().undo());
            expect(tracks.getChordTrackId() == INVALID_TRACK_ID);
            expect(UndoManager::getInstance().redo());
            expect(tracks.getChordTrackId() == trackId);
            expect(clips.getClipsOnTrack(trackId) == ids);

            const auto cleared =
                fixture.run("chordTrack.replaceProgression",
                            object({{"chords", juce::var(juce::Array<juce::var>{})}}));
            expect(cleared.ok);
            expect(clips.getClipsOnTrack(trackId).empty());
            expect(UndoManager::getInstance().undo());
            expect(clips.getClipsOnTrack(trackId) == ids);
        }

        beginTest("Replacing an existing chord progression restores its clips on undo");
        {
            Fixture fixture;
            auto& clips = ClipManager::getInstance();
            const auto trackId = TrackManager::getInstance().ensureChordTrack();
            const auto oldId = clips.createMidiClipBeats(trackId, 2.0, 4.0);
            clips.addChordAnnotation(oldId, {0.0, 4.0, "Fmaj7", 9});
            const auto oldClip = *clips.getClip(oldId);

            juce::Array<juce::var> progression;
            progression.add(object(
                {{"startBeat", 0.0}, {"lengthBeats", 2.0}, {"root", "D"}, {"quality", "min"}}));
            const auto replaced = fixture.run("chordTrack.replaceProgression",
                                              object({{"chords", juce::var(progression)}}));
            expect(replaced.ok, replaced.error.message);
            expect(clips.getClip(oldId) == nullptr);
            const auto replacementIds = clips.getClipsOnTrack(trackId);
            expect(replacementIds.size() == 1 && replacementIds.front() != oldId);

            expect(UndoManager::getInstance().undo());
            const auto* restored = clips.getClip(oldId);
            expect(restored != nullptr && restored->chordAnnotations == oldClip.chordAnnotations);
            expect(clips.getClipsOnTrack(trackId) == std::vector<ClipId>{oldId});
            expect(UndoManager::getInstance().redo());
            expect(clips.getClipsOnTrack(trackId) == replacementIds);
        }

        beginTest("Chord detection is range-bounded, read-only, and reports confidence");
        {
            Fixture fixture;
            auto& tracks = TrackManager::getInstance();
            auto& clips = ClipManager::getInstance();
            const auto sourceTrack = tracks.createTrack("Harmony", TrackType::Media);
            const auto sourceClip = clips.createMidiClipBeats(sourceTrack, 0.0, 8.0);
            auto* clip = clips.getClip(sourceClip);
            expect(clip != nullptr);
            if (clip == nullptr)
                return;
            for (int note : {60, 64, 67})
                clip->midiNotes.push_back(makeMidiNote(note, 0.0, 4.0));
            for (int note : {67, 71, 74})
                clip->midiNotes.push_back(makeMidiNote(note, 4.0, 4.0));
            clips.forceNotifyClipPropertyChanged(sourceClip);

            const auto before = fixture.service.currentRevision();
            const auto detected =
                fixture.run("chordTrack.detect", object({{"sourceClipId", sourceClip},
                                                         {"startBeat", 0.0},
                                                         {"endBeat", 8.0},
                                                         {"windowBeats", 4.0}}));
            expect(detected.ok, detected.error.message);
            expect(detected.revision == before);
            const auto* chords = detected.result["chords"].getArray();
            expect(chords != nullptr && chords->size() == 2);
            if (chords != nullptr && chords->size() == 2) {
                expectEquals((*chords)[0]["root"].toString(), juce::String("C"));
                expectEquals((*chords)[1]["root"].toString(), juce::String("G"));
                expectWithinAbsoluteError(static_cast<double>((*chords)[0]["confidence"]), 1.0,
                                          1.0e-9);
            }

            const auto invalid =
                fixture.run("chordTrack.detect", object({{"sourceClipId", sourceClip},
                                                         {"startBeat", 0.0},
                                                         {"endBeat", 9.0},
                                                         {"windowBeats", 4.0}}));
            expect(!invalid.ok);
            expect(fixture.service.currentRevision() == before);

            const auto unsupportedClip = clips.createMidiClipBeats(sourceTrack, 12.0, 4.0);
            auto* unsupported = clips.getClip(unsupportedClip);
            expect(unsupported != nullptr);
            if (unsupported != nullptr) {
                unsupported->midiNotes.push_back(makeMidiNote(61, 0.0, 4.0));
                clips.forceNotifyClipPropertyChanged(unsupportedClip);
            }
            const auto beforeUnsupported = fixture.service.currentRevision();
            const auto none =
                fixture.run("chordTrack.detect", object({{"sourceClipId", unsupportedClip},
                                                         {"startBeat", 0.0},
                                                         {"endBeat", 4.0},
                                                         {"windowBeats", 4.0}}));
            expect(none.ok);
            expect(none.revision == beforeUnsupported);
            expect(none.result["chords"].getArray()->isEmpty());
            expect(none.result["warnings"].getArray()->size() == 1);
            expectEquals(none.result["warnings"][0].toString(),
                         juce::String("no_supported_chords"));
        }

        beginTest("Chord extraction applies populated policies atomically");
        {
            Fixture fixture;
            auto& tracks = TrackManager::getInstance();
            auto& clips = ClipManager::getInstance();
            const auto sourceTrack = tracks.createTrack("Harmony", TrackType::Media);
            const auto sourceClip = clips.createMidiClipBeats(sourceTrack, 0.0, 8.0);
            auto* clip = clips.getClip(sourceClip);
            expect(clip != nullptr);
            if (clip == nullptr)
                return;
            for (int note : {60, 64, 67})
                clip->midiNotes.push_back(makeMidiNote(note, 0.0, 4.0));
            for (int note : {67, 71, 74})
                clip->midiNotes.push_back(makeMidiNote(note, 4.0, 4.0));
            clips.forceNotifyClipPropertyChanged(sourceClip);

            auto extraction = [&](double destination, const char* policy) {
                return fixture.run("chordTrack.extract",
                                   object({{"sourceClipId", sourceClip},
                                           {"startBeat", 0.0},
                                           {"endBeat", 8.0},
                                           {"windowBeats", 4.0},
                                           {"destinationStartBeat", destination},
                                           {"populatedPolicy", policy},
                                           {"voicing", "root"},
                                           {"octave", 4}}));
            };

            fixture.service.changes().flush();
            std::vector<ChangeSource::Change> seen;
            fixture.service.changes().addListener(
                [&seen](const std::vector<ChangeSource::Change>& changes) {
                    seen.insert(seen.end(), changes.begin(), changes.end());
                });
            const auto before = fixture.service.currentRevision();
            const auto first = extraction(0.0, "fail");
            expect(first.ok, first.error.message);
            expect(first.revision == before + 1);
            fixture.service.changes().flush();
            expect(hasTopic(seen, Topic::Tracks));
            expect(hasTopic(seen, Topic::Clips));
            expect(hasTopic(seen, Topic::Devices));
            const auto chordTrackId = tracks.getChordTrackId();
            expect(chordTrackId != INVALID_TRACK_ID);
            const auto firstId =
                static_cast<ClipId>(static_cast<int>(first.result["createdClipId"]));
            const auto* firstClip = clips.getClip(firstId);
            expect(firstClip != nullptr && firstClip->chordAnnotations.size() == 2);
            expect(firstClip != nullptr && firstClip->midiNotes.size() == 6);

            const auto failed = extraction(16.0, "fail");
            expect(!failed.ok);
            expect(failed.revision == first.revision);
            const auto merged = extraction(16.0, "merge");
            expect(merged.ok, merged.error.message);
            expect(clips.getClipsOnTrack(chordTrackId).size() == 2);
            const auto beforeOverlap = fixture.service.currentRevision();
            const auto overlap = extraction(4.0, "merge");
            expect(!overlap.ok);
            expect(fixture.service.currentRevision() == beforeOverlap);

            const auto idsBeforeReplace = clips.getClipsOnTrack(chordTrackId);
            const auto replaced = extraction(32.0, "replace");
            expect(replaced.ok, replaced.error.message);
            expect(clips.getClipsOnTrack(chordTrackId).size() == 1);
            expect(UndoManager::getInstance().undo());
            expect(clips.getClipsOnTrack(chordTrackId) == idsBeforeReplace);
            expect(UndoManager::getInstance().redo());
            expect(clips.getClipsOnTrack(chordTrackId).size() == 1);
        }

        beginTest("Sending a progression respects target and occupied-range policies");
        {
            Fixture fixture;
            auto& tracks = TrackManager::getInstance();
            auto& clips = ClipManager::getInstance();
            const auto chordTrackId = tracks.ensureChordTrack();
            const auto chordClipId = clips.createMidiClipBeats(chordTrackId, 0.0, 4.0);
            auto* chordClip = clips.getClip(chordClipId);
            expect(chordClip != nullptr);
            if (chordClip == nullptr)
                return;
            chordClip->chordAnnotations.push_back({0.0, 4.0, "C4 maj", 1});
            for (int note : {60, 64, 67}) {
                auto midiNote = makeMidiNote(note, 0.0, 4.0);
                midiNote.chordGroup = 1;
                chordClip->midiNotes.push_back(midiNote);
            }
            clips.forceNotifyClipPropertyChanged(chordClipId);

            const auto targetTrackId = tracks.createTrack("Target", TrackType::Media);
            const auto occupant = clips.createMidiClipBeats(targetTrackId, 8.0, 4.0);
            auto send = [&](const char* occupiedPolicy, const char* instrumentPolicy) {
                return fixture.run("chordTrack.sendToTrack",
                                   object({{"sourceClipId", chordClipId},
                                           {"targetTrackId", targetTrackId},
                                           {"startBeat", 8.0},
                                           {"occupiedPolicy", occupiedPolicy},
                                           {"voicing", "source"},
                                           {"instrumentPolicy", instrumentPolicy}}));
            };

            fixture.service.changes().flush();
            std::vector<ChangeSource::Change> seen;
            fixture.service.changes().addListener(
                [&seen](const std::vector<ChangeSource::Change>& changes) {
                    seen.insert(seen.end(), changes.begin(), changes.end());
                });
            const auto before = fixture.service.currentRevision();
            const auto noInstrument = send("replace", "require_existing");
            expect(!noInstrument.ok);
            expect(noInstrument.revision == before);
            const auto occupiedFailure = send("fail", "preserve_target");
            expect(!occupiedFailure.ok);
            expect(occupiedFailure.revision == before);

            const auto sent = send("replace", "preserve_target");
            expect(sent.ok, sent.error.message);
            fixture.service.changes().flush();
            expect(hasTopic(seen, Topic::Clips));
            const auto sentId = static_cast<ClipId>(static_cast<int>(sent.result["id"]));
            const auto* baked = clips.getClip(sentId);
            expect(baked != nullptr && baked->chordAnnotations.empty());
            expect(baked != nullptr && baked->midiNotes.size() == 3);
            if (baked != nullptr)
                expect(std::ranges::all_of(baked->midiNotes,
                                           [](const auto& note) { return note.chordGroup == 0; }));
            expect(clips.getClip(occupant) == nullptr);
            expect(UndoManager::getInstance().undo());
            expect(clips.getClip(sentId) == nullptr);
            expect(clips.getClip(occupant) != nullptr);
            expect(UndoManager::getInstance().redo());
            expect(clips.getClip(sentId) != nullptr);
        }

        beginTest("A live write advances the revision exactly once");
        {
            Fixture fixture;
            const auto before = fixture.service.currentRevision();
            const auto response =
                fixture.run("tracks.create", object({{"name", "Live"}, {"type", "audio"}}));
            expect(response.ok);

            // The live facade notifies TrackManager listeners synchronously from
            // inside the handler, so the bridge sees tracksChanged() while the
            // request is still executing. Without the re-entrancy guard that
            // callback would bump the revision too.
            expect(fixture.service.currentRevision() == before + 1);
            expect(response.revision == before + 1);
        }

        beginTest("A live multi-field update advances the revision exactly once");
        {
            Fixture fixture;
            const auto created =
                fixture.run("tracks.create", object({{"name", "Strip"}, {"type", "audio"}}));
            expect(created.ok);
            const auto trackId = static_cast<int>(created.result["id"]);
            const auto before = fixture.service.currentRevision();

            const auto updated = fixture.run("tracks.update", object({{"trackId", trackId},
                                                                      {"name", "Renamed"},
                                                                      {"volume", 0.5},
                                                                      {"pan", -0.25},
                                                                      {"muted", true},
                                                                      {"soloed", true}}));
            expect(updated.ok);
            expect(fixture.service.currentRevision() == before + 1);
        }

        beginTest("Track colour and input state update atomically and undo together");
        {
            Fixture fixture;
            const auto created =
                fixture.run("tracks.create", object({{"name", "Input"}, {"type", "audio"}}));
            expect(created.ok);
            const auto trackId = static_cast<TrackId>(static_cast<int>(created.result["id"]));
            const auto* original = TrackManager::getInstance().getTrack(trackId);
            expect(original != nullptr);
            const auto originalColour = original != nullptr ? original->colour.getARGB() : 0u;
            const auto colour = static_cast<juce::int64>(0xff102030u);
            const auto before = fixture.service.currentRevision();

            const auto updated =
                fixture.run("tracks.update", object({{"trackId", static_cast<int>(trackId)},
                                                     {"colourArgb", colour},
                                                     {"recordArmed", true},
                                                     {"inputMonitor", "in"}}));
            expect(updated.ok);
            expect(static_cast<juce::int64>(updated.result["colourArgb"]) == colour);
            expect(static_cast<bool>(updated.result["recordArmed"]));
            expectEquals(updated.result["inputMonitor"].toString(), juce::String("in"));
            expect(fixture.service.currentRevision() == before + 1);

            const auto beforeNoOp = fixture.service.currentRevision();
            expect(fixture
                       .run("tracks.update", object({{"trackId", static_cast<int>(trackId)},
                                                     {"colourArgb", colour},
                                                     {"recordArmed", true},
                                                     {"inputMonitor", "in"}}))
                       .ok);
            expect(fixture.service.currentRevision() == beforeNoOp);

            expect(UndoManager::getInstance().undo());
            const auto* restored = TrackManager::getInstance().getTrack(trackId);
            expect(restored != nullptr);
            if (restored != nullptr) {
                expect(restored->colour.getARGB() == originalColour);
                expect(!restored->recordArmed);
                expect(restored->inputMonitor == InputMonitorMode::Off);
            }
        }

        beginTest("Input state is rejected atomically for input-less tracks");
        {
            Fixture fixture;
            const auto created =
                fixture.run("tracks.create", object({{"name", "Bus"}, {"type", "group"}}));
            expect(created.ok);
            const auto trackId = static_cast<TrackId>(static_cast<int>(created.result["id"]));
            const auto before = fixture.service.currentRevision();

            const auto rejected =
                fixture.run("tracks.update", object({{"trackId", static_cast<int>(trackId)},
                                                     {"name", "Partially changed"},
                                                     {"recordArmed", true}}));
            expect(!rejected.ok);
            expectEquals(toString(rejected.error.code), juce::String("conflict"));
            expect(fixture.service.currentRevision() == before);
            const auto* track = TrackManager::getInstance().getTrack(trackId);
            expect(track != nullptr);
            if (track != nullptr) {
                expectEquals(track->name, juce::String("Bus"));
                expect(!track->recordArmed);
            }
        }

        beginTest("A patch that changes nothing does not advance the revision");
        {
            Fixture fixture;
            const auto created =
                fixture.run("tracks.create", object({{"name", "Same"}, {"type", "audio"}}));
            expect(created.ok);
            const auto trackId = static_cast<int>(created.result["id"]);
            const auto before = fixture.service.currentRevision();

            // trackId alone is a valid patch, and so is one that restates the
            // current values. Neither enqueues a command, so neither is a
            // committed write.
            expect(fixture.run("tracks.update", object({{"trackId", trackId}})).ok);
            expect(
                fixture.run("tracks.update", object({{"trackId", trackId}, {"name", "Same"}})).ok);
            expect(fixture.service.currentRevision() == before);
        }

        beginTest("A UI-originated change still advances the revision");
        {
            Fixture fixture;
            const auto before = fixture.service.currentRevision();
            // Not routed through the dispatcher, so the re-entrancy guard must
            // not suppress it — this is what the bridge exists for.
            TrackManager::getInstance().createTrack("By hand", TrackType::Media);
            expect(fixture.service.currentRevision() > before);
        }

        beginTest("Automation-driven mixer motion does not advance the revision");
        {
            Fixture fixture;
            const auto trackId =
                TrackManager::getInstance().createTrack("Automated", TrackType::Media);
            const auto before = fixture.service.currentRevision();

            {
                AutomationManager::AutomationWriteScope automationWrite;
                TrackManager::getInstance().setTrackVolume(trackId, 0.5f, true);
            }

            expect(fixture.service.currentRevision() == before);
        }

        beginTest("A local project-property edit advances the revision");
        {
            Fixture fixture;
            auto& project = ProjectManager::getInstance();
            const auto originalTempo = project.getCurrentProjectInfo().tempo;
            const auto nextTempo = originalTempo == 123.0 ? 124.0 : 123.0;
            const auto before = fixture.service.currentRevision();

            project.setTempo(nextTempo);

            expect(fixture.service.currentRevision() == before + 1);
            project.setTempo(originalTempo);
        }

        beginTest("Project loop range updates the engine and project as one undoable edit");
        {
            Fixture fixture;
            auto& projects = ProjectManager::getInstance();
            const auto originalStart = projects.getCurrentProjectInfo().loopStartBeats;
            const auto originalEnd = projects.getCurrentProjectInfo().loopEndBeats;
            const auto newStart = originalStart == 4.0 ? 8.0 : 4.0;
            const auto newEnd = newStart + 8.0;
            int engineWrites = 0;
            double engineStart = -1.0;
            double engineEnd = -1.0;
            fixture.api.setProjectLoopRangeWriter([&](double start, double end) {
                ++engineWrites;
                engineStart = start;
                engineEnd = end;
            });

            const auto beforeInvalid = fixture.service.currentRevision();
            const auto invalid =
                fixture.run("project.setLoopRange", object({{"startBeat", 8.0}, {"endBeat", 8.0}}));
            expect(!invalid.ok);
            expectEquals(toString(invalid.error.code), juce::String("validation_failed"));
            expect(fixture.service.currentRevision() == beforeInvalid);
            expectEquals(engineWrites, 0);

            const auto updated = fixture.run(
                "project.setLoopRange", object({{"startBeat", newStart}, {"endBeat", newEnd}}));
            expect(updated.ok);
            expect(static_cast<double>(updated.result["loopStartBeats"]) == newStart);
            expect(static_cast<double>(updated.result["loopEndBeats"]) == newEnd);
            expect(fixture.service.currentRevision() == beforeInvalid + 1);
            expectEquals(engineWrites, 1);
            expect(engineStart == newStart && engineEnd == newEnd);

            const auto beforeNoOp = fixture.service.currentRevision();
            expect(fixture
                       .run("project.setLoopRange",
                            object({{"startBeat", newStart}, {"endBeat", newEnd}}))
                       .ok);
            expect(fixture.service.currentRevision() == beforeNoOp);
            expectEquals(engineWrites, 1);

            expect(UndoManager::getInstance().undo());
            const auto& restored = projects.getCurrentProjectInfo();
            expect(restored.loopStartBeats == originalStart);
            expect(restored.loopEndBeats == originalEnd);
            expectEquals(engineWrites, 2);
            expect(engineStart == originalStart && engineEnd == originalEnd);
        }

        beginTest("A live write is reachable through the real facade");
        {
            Fixture fixture;
            const auto response =
                fixture.run("tracks.create", object({{"name", "Verified"}, {"type", "audio"}}));
            expect(response.ok);

            const auto trackId = static_cast<TrackId>(static_cast<int>(response.result["id"]));
            const auto* track = TrackManager::getInstance().getTrack(trackId);
            expect(track != nullptr);
            if (track != nullptr)
                expectEquals(track->name, juce::String("Verified"));

            const auto fetched =
                fixture.run("tracks.get", object({{"trackId", static_cast<int>(trackId)}}));
            expect(fetched.ok);
            expectEquals(fetched.result["name"].toString(), juce::String("Verified"));
        }

        beginTest("A remote mutation is undoable as one step");
        {
            Fixture fixture;
            expect(!UndoManager::getInstance().canUndo());

            const auto response =
                fixture.run("tracks.create", object({{"name", "Undoable"}, {"type", "audio"}}));
            expect(response.ok);
            const auto trackId = static_cast<TrackId>(static_cast<int>(response.result["id"]));
            expect(TrackManager::getInstance().getTrack(trackId) != nullptr);

            // The acceptance criterion: one remote mutation, one Undo.
            expect(UndoManager::getInstance().canUndo());
            UndoManager::getInstance().undo();
            expect(TrackManager::getInstance().getTrack(trackId) == nullptr);
        }

        beginTest("A multi-field update is undone as one step");
        {
            Fixture fixture;
            const auto created =
                fixture.run("tracks.create", object({{"name", "Mixer"}, {"type", "audio"}}));
            expect(created.ok);
            const auto trackId = static_cast<TrackId>(static_cast<int>(created.result["id"]));

            expect(fixture
                       .run("tracks.update", object({{"trackId", static_cast<int>(trackId)},
                                                     {"name", "Renamed"},
                                                     {"muted", true},
                                                     {"soloed", true}}))
                       .ok);

            const auto* updated = TrackManager::getInstance().getTrack(trackId);
            expect(updated != nullptr && updated->muted && updated->soloed);

            // Several commands, one compound: a single Undo restores every field.
            UndoManager::getInstance().undo();
            const auto* restored = TrackManager::getInstance().getTrack(trackId);
            expect(restored != nullptr);
            if (restored != nullptr) {
                expectEquals(restored->name, juce::String("Mixer"));
                expect(!restored->muted);
                expect(!restored->soloed);
            }
        }

        beginTest("A cascading write notifies every affected topic");
        {
            Fixture fixture;
            const auto created =
                fixture.run("tracks.create", object({{"name", "Doomed"}, {"type", "audio"}}));
            expect(created.ok);
            const auto trackId = static_cast<int>(created.result["id"]);

            std::vector<ChangeSource::Change> seen;
            fixture.service.changes().addListener(
                [&seen](const std::vector<ChangeSource::Change>& changes) {
                    seen.insert(seen.end(), changes.begin(), changes.end());
                });

            expect(fixture.run("tracks.delete", object({{"trackId", trackId}})).ok);
            fixture.service.changes().flush();

            // Deleting a track takes its clips and devices with it.
            expect(hasTopic(seen, Topic::Tracks));
            expect(hasTopic(seen, Topic::Clips));
            expect(hasTopic(seen, Topic::Devices));
        }

        beginTest("A client can create a track, add a clip, and add notes to it");
        {
            Fixture fixture;
            const auto track =
                fixture.run("tracks.create", object({{"name", "Lead"}, {"type", "audio"}}));
            expect(track.ok);
            const auto trackId = static_cast<int>(track.result["id"]);

            const auto clip = fixture.run(
                "clips.createMidi", object({{"lengthBeats", 4.0},
                                            {"placement", arrangementDestination(trackId, 0.0)}}));
            expect(clip.ok);
            const auto clipId = static_cast<int>(clip.result["id"]);

            const auto note = fixture.run("clips.addMidiNote", object({{"clipId", clipId},
                                                                       {"note", 60},
                                                                       {"velocity", 100},
                                                                       {"startBeat", 0.0},
                                                                       {"lengthBeats", 1.0}}));
            expect(note.ok);
            if (auto* notes = note.result["notes"].getArray())
                expectEquals(notes->size(), 1);
            else
                expect(false, "addMidiNote returned no notes array");

            const auto listed = fixture.run("clips.list", object({{"trackId", trackId}}));
            expect(listed.ok);
            if (auto* clips = listed.result.getArray())
                expectEquals(clips->size(), 1);

            expect(fixture.service.currentRevision() == 3);
            expect(UndoManager::getInstance().canUndo());
        }

        beginTest("Loading a sample places an audio clip as long as the file");
        {
            Fixture fixture;
            FlatTempoMap tempo(120.0);
            fixture.api.setProjectTempoMap([&tempo] { return &tempo; });
            const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("magda-load-sample-test");
            dir.createDirectory();
            const auto sample = writeSample(dir.getChildFile("loop.wav"), 2.0);
            const auto track =
                fixture.run("tracks.create", object({{"name", "Loop"}, {"type", "audio"}}));
            const auto trackId = static_cast<int>(track.result["id"]);

            const auto loaded = fixture.run(
                "clips.loadSample", object({{"samplePath", sample.getFullPathName()},
                                            {"placement", arrangementDestination(trackId, 4.0)}}));
            expect(loaded.ok);
            const auto* clip = ClipManager::getInstance().getClip(
                static_cast<ClipId>(static_cast<int>(loaded.result["id"])));
            expect(clip != nullptr && clip->isAudio());
            if (clip != nullptr) {
                expectWithinAbsoluteError(clip->placement.startBeat, 4.0, 1e-9);
                // Two seconds at 120 BPM.
                expectWithinAbsoluteError(clip->placement.lengthBeats, 4.0, 1e-6);
            }

            const auto notAudio = dir.getChildFile("notes.txt");
            notAudio.replaceWithText("not a sample");
            const auto lastUndo = UndoManager::getInstance().getUndoDescription();
            const auto refused = fixture.run(
                "clips.loadSample", object({{"samplePath", notAudio.getFullPathName()},
                                            {"placement", arrangementDestination(trackId, 12.0)}}));
            expect(!refused.ok);
            expect(refused.error.code == ErrorCode::ValidationFailed);
            expectEquals(static_cast<int>(ClipManager::getInstance()
                                              .getClipsOnTrack(static_cast<TrackId>(trackId),
                                                               ClipView::Arrangement)
                                              .size()),
                         1);
            expectEquals(UndoManager::getInstance().getUndoDescription(), lastUndo);
            dir.deleteRecursively();
        }

        beginTest("An audio clip's playback, stretch, reverse, and fades update as one patch");
        {
            Fixture fixture;
            FlatTempoMap tempo(120.0);
            fixture.api.setProjectTempoMap([&tempo] { return &tempo; });
            const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("magda-update-audio-test");
            dir.createDirectory();
            const auto sample = writeSample(dir.getChildFile("loop.wav"), 2.0);
            const auto trackId = static_cast<int>(
                fixture.run("tracks.create", object({{"name", "Loop"}, {"type", "audio"}}))
                    .result["id"]);
            const auto clipId = static_cast<int>(
                fixture
                    .run("clips.loadSample",
                         object({{"samplePath", sample.getFullPathName()},
                                 {"placement", arrangementDestination(trackId, 0.0)}}))
                    .result["id"]);

            const auto patch = object({{"clipId", clipId},
                                       {"sourceBpm", 90.0},
                                       {"playback", "beat"},
                                       {"stretch", "soundtouch_hq"},
                                       {"reversed", true},
                                       {"fadeInSeconds", 0.25},
                                       {"fadeOutSeconds", 0.5},
                                       {"fadeInCurve", "convex"},
                                       {"fadeOutCurve", "s_curve"}});
            const auto updated = fixture.run("clips.updateAudio", patch);
            expect(updated.ok);
            const auto* clip = ClipManager::getInstance().getClip(static_cast<ClipId>(clipId));
            const auto* event = clip != nullptr ? clip->primaryEvent() : nullptr;
            expect(event != nullptr);
            if (event != nullptr) {
                expectWithinAbsoluteError(event->interpBpm, 90.0, 1e-9);
                expect(event->playbackIntent == PlaybackIntent::Beat);
                expectEquals(event->timeStretchMode, 4);
                expect(event->reversed);
                expectWithinAbsoluteError(event->fadeInSeconds, 0.25, 1e-9);
                expectWithinAbsoluteError(event->fadeOutSeconds, 0.5, 1e-9);
                expectEquals(event->fadeInType, static_cast<int>(FadeCurve::Convex));
                expectEquals(event->fadeOutType, static_cast<int>(FadeCurve::SCurve));
            }

            const auto revision = fixture.service.currentRevision();
            expect(fixture.run("clips.updateAudio", patch).ok);
            expect(fixture.service.currentRevision() == revision,
                   "A patch restating current values changes nothing");

            const auto midiId = static_cast<int>(
                fixture
                    .run("clips.createMidi",
                         object({{"lengthBeats", 4.0},
                                 {"placement", arrangementDestination(trackId, 8.0)}}))
                    .result["id"]);
            const auto refused =
                fixture.run("clips.updateAudio", object({{"clipId", midiId}, {"reversed", true}}));
            expect(!refused.ok && refused.error.code == ErrorCode::Conflict);
            dir.deleteRecursively();
        }

        beginTest("Looping a free audio clip puts it in beat playback, as LOOP does (#3060)");
        {
            Fixture fixture;
            FlatTempoMap tempo(120.0);
            fixture.api.setProjectTempoMap([&tempo] { return &tempo; });
            const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("magda-loop-beat-test");
            dir.createDirectory();
            const auto sample = writeSample(dir.getChildFile("loop.wav"), 2.0);
            const auto trackId = static_cast<int>(
                fixture.run("tracks.create", object({{"name", "Loop"}, {"type", "audio"}}))
                    .result["id"]);
            const auto clipId = static_cast<int>(
                fixture
                    .run("clips.loadSample",
                         object({{"samplePath", sample.getFullPathName()},
                                 {"placement", arrangementDestination(trackId, 0.0)}}))
                    .result["id"]);
            const auto free = fixture.run("clips.get", object({{"clipId", clipId}}));
            expect(free.ok);
            expect(free.result["audio"]["playback"].toString() == "free");
            expect(!static_cast<bool>(free.result["audio"]["looping"]));

            const auto looped =
                fixture.run("clips.updateAudio", object({{"clipId", clipId}, {"looping", true}}));
            expect(looped.ok);
            const auto& audio = looped.result["audio"];
            expect(audio["playback"].toString() == "beat");
            expect(static_cast<bool>(audio["looping"]));
            expectWithinAbsoluteError(static_cast<double>(audio["tempo"]), 120.0, 1e-9);
            expect(audio["tempoFrom"].toString() == "project");

            const auto unlooped = fixture.run("clips.updateAudio",
                                              object({{"clipId", clipId}, {"playback", "free"}}));
            expect(unlooped.ok);
            expect(!static_cast<bool>(unlooped.result["audio"]["looping"]));

            const auto contradictory =
                fixture.run("clips.updateAudio",
                            object({{"clipId", clipId}, {"playback", "free"}, {"looping", true}}));
            expect(!contradictory.ok && contradictory.error.code == ErrorCode::ValidationFailed);
            dir.deleteRecursively();
        }

        beginTest("An audio clip reads its level, fades and source, and takes volume and gain "
                  "(#3338)");
        {
            Fixture fixture;
            FlatTempoMap tempo(120.0);
            fixture.api.setProjectTempoMap([&tempo] { return &tempo; });
            const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("magda-clip-level-test");
            dir.createDirectory();
            const auto sample = writeSample(dir.getChildFile("voice.wav"), 2.0);
            const auto trackId = static_cast<int>(
                fixture.run("tracks.create", object({{"name", "Voice"}, {"type", "audio"}}))
                    .result["id"]);
            const auto clipId = static_cast<int>(
                fixture
                    .run("clips.loadSample",
                         object({{"samplePath", sample.getFullPathName()},
                                 {"placement", arrangementDestination(trackId, 0.0)}}))
                    .result["id"]);

            const auto set =
                fixture.run("clips.updateAudio", object({{"clipId", clipId},
                                                         {"volumeDb", -6.0},
                                                         {"gainDb", 3.0},
                                                         {"fadeInSeconds", 0.25},
                                                         {"fadeOutCurve", "concave"}}));
            expect(set.ok);
            const auto read = fixture.run("clips.get", object({{"clipId", clipId}}));
            expect(read.ok);
            const auto audio = read.result["audio"];
            expectWithinAbsoluteError(static_cast<double>(audio["volumeDb"]), -6.0, 1e-6);
            expectWithinAbsoluteError(static_cast<double>(audio["gainDb"]), 3.0, 1e-6);
            expectWithinAbsoluteError(static_cast<double>(audio["fadeInSeconds"]), 0.25, 1e-9);
            expect(audio["fadeInCurve"].toString() == "linear");
            expect(audio["fadeOutCurve"].toString() == "concave");

            const auto revision = fixture.service.currentRevision();
            const auto clamped =
                fixture.run("clips.updateAudio",
                            object({{"clipId", clipId}, {"volumeDb", 12.0}, {"gainDb", 40.0}}));
            expect(clamped.ok);
            expectEquals(static_cast<double>(clamped.result["audio"]["volumeDb"]), 0.0);
            expectEquals(static_cast<double>(clamped.result["audio"]["gainDb"]), 24.0);
            expect(fixture.service.currentRevision() == revision + 1);
            expect(fixture
                       .run("clips.updateAudio",
                            object({{"clipId", clipId}, {"volumeDb", 3.0}, {"gainDb", 99.0}}))
                       .ok);
            expect(fixture.service.currentRevision() == revision + 1,
                   "A value that clamps to the current one changes nothing");
            expect(UndoManager::getInstance().undo());
            const auto* clip = ClipManager::getInstance().getClip(static_cast<ClipId>(clipId));
            expect(clip != nullptr);
            if (clip != nullptr) {
                expectWithinAbsoluteError(clip->volumeDB, -6.0f, 1e-6f);
                expectWithinAbsoluteError(clip->gainDB, 3.0f, 1e-6f);
            }

            // A gain envelope is replaced whole, sorted, with -60 dB as silence (#3064).
            const auto point = [](double seconds, double db, const char* curve) {
                return object({{"sourceSeconds", seconds}, {"gainDb", db}, {"curve", curve}});
            };
            const auto envelope =
                object({{"clipId", clipId},
                        {"gainEnvelope", juce::var(juce::Array<juce::var>{
                                             point(1.5, 3.0, "step"), point(0.5, -6.0, "linear"),
                                             point(0.0, -100.0, "linear")})}});
            const auto shaped = fixture.run("clips.updateAudio", envelope);
            expect(shaped.ok);
            const auto* points = shaped.result["audio"]["gainEnvelope"].getArray();
            expect(points != nullptr && points->size() == 3);
            if (points != nullptr && points->size() == 3) {
                expectEquals(static_cast<double>(points->getReference(0)["gainDb"]), -60.0);
                expectWithinAbsoluteError(static_cast<double>(points->getReference(1)["gainDb"]),
                                          -6.0, 1e-9);
                expectWithinAbsoluteError(
                    static_cast<double>(points->getReference(2)["sourceSeconds"]), 1.5, 1e-9);
                expect(points->getReference(2)["curve"].toString() == "step");
            }
            const auto shapedRevision = fixture.service.currentRevision();
            expect(fixture.run("clips.updateAudio", envelope).ok);
            expect(fixture.service.currentRevision() == shapedRevision,
                   "Restating the envelope changes nothing");
            const auto cleared =
                fixture.run("clips.updateAudio",
                            object({{"clipId", clipId},
                                    {"gainEnvelope", juce::var(juce::Array<juce::var>{})}}));
            expect(cleared.ok);
            expect(cleared.result["audio"]["gainEnvelope"].getArray()->isEmpty());

            // Trimming the head moves where the clip reads in its file.
            expect(fixture
                       .run("clips.resize",
                            object({{"clipId", clipId}, {"lengthBeats", 2.0}, {"edge", "start"}}))
                       .ok);
            const auto trimmed = fixture.run("clips.get", object({{"clipId", clipId}}));
            expect(trimmed.ok);
            const auto& source = trimmed.result["audio"]["source"];
            const auto* resized = ClipManager::getInstance().getClip(static_cast<ClipId>(clipId));
            const auto* event = resized != nullptr ? resized->primaryEvent() : nullptr;
            expect(event != nullptr);
            if (event != nullptr) {
                expect(source["path"].toString() == event->sourceFilePath());
                // The API resolves symlinks, and macOS's temp folder sits under one (/var).
                std::error_code ec;
                expect(std::filesystem::equivalent(source["path"].toString().toStdString(),
                                                   sample.getFullPathName().toStdString(), ec));
                expectWithinAbsoluteError(static_cast<double>(source["sampleRate"]), 48000.0, 1e-9);
                expectWithinAbsoluteError(static_cast<double>(source["lengthSeconds"]), 2.0, 1e-6);
                const auto& trimmedAudio = trimmed.result["audio"];
                expectWithinAbsoluteError(static_cast<double>(trimmedAudio["sourceStartSeconds"]),
                                          event->anchorSeconds(), 1e-9);
                expectWithinAbsoluteError(static_cast<double>(trimmedAudio["sourceStartSeconds"]),
                                          1.0, 1e-6);
                expectWithinAbsoluteError(static_cast<double>(trimmedAudio["sourceLengthSeconds"]),
                                          1.0, 1e-6);
            }
            dir.deleteRecursively();
        }

        beginTest("An audio clip splits at several beats and joins back as one undo step each "
                  "(#3345)");
        {
            Fixture fixture;
            FlatTempoMap tempo(120.0);
            fixture.api.setProjectTempoMap([&tempo] { return &tempo; });
            const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("magda-split-join-test");
            dir.createDirectory();
            const auto sample = writeSample(dir.getChildFile("voice.wav"), 2.0);
            const auto trackId = static_cast<int>(
                fixture.run("tracks.create", object({{"name", "Voice"}, {"type", "audio"}}))
                    .result["id"]);
            const auto clipId = static_cast<int>(
                fixture
                    .run("clips.loadSample",
                         object({{"samplePath", sample.getFullPathName()},
                                 {"placement", arrangementDestination(trackId, 0.0)}}))
                    .result["id"]);

            const auto beats = juce::var(juce::Array<juce::var>{3.0, 1.0, 3.0});
            const auto split =
                fixture.run("clips.split", object({{"clipId", clipId}, {"beats", beats}}));
            expect(split.ok);
            const auto* pieces = split.result.getArray();
            expect(pieces != nullptr && pieces->size() == 3);
            if (pieces != nullptr && pieces->size() == 3) {
                expectEquals(static_cast<int>(pieces->getReference(0)["id"]), clipId);
                const double starts[] = {0.0, 0.5, 1.5};
                for (int i = 0; i < 3; ++i)
                    expectWithinAbsoluteError(
                        static_cast<double>(pieces->getReference(i)["audio"]["sourceStartSeconds"]),
                        starts[i], 1e-6);
            }
            expect(UndoManager::getInstance().undo());
            expectEquals(static_cast<int>(ClipManager::getInstance()
                                              .getClipsOnTrack(static_cast<TrackId>(trackId),
                                                               ClipView::Arrangement)
                                              .size()),
                         1);
            // A redo would split again under new ids, so split through the API to know them.
            const auto again =
                fixture.run("clips.split", object({{"clipId", clipId}, {"beats", beats}}));
            juce::Array<juce::var> pieceIds;
            if (const auto* items = again.result.getArray())
                for (const auto& item : *items)
                    pieceIds.add(item["id"]);
            expect(pieceIds.size() == 3);

            const auto before = fixture.service.currentRevision();
            const auto onEdge = fixture.run(
                "clips.split",
                object({{"clipId", clipId}, {"beats", juce::var(juce::Array<juce::var>{1.0})}}));
            expect(!onEdge.ok && onEdge.error.code == ErrorCode::ValidationFailed);
            expect(fixture.service.currentRevision() == before);

            // The last piece's fade-out ends the joined clip.
            expect(fixture
                       .run("clips.updateAudio",
                            object({{"clipId", pieceIds[2]}, {"fadeOutSeconds", 0.1}}))
                       .ok);
            juce::Array<juce::var> shuffled{pieceIds[2], pieceIds[0], pieceIds[1]};
            const auto joined =
                fixture.run("clips.join", object({{"clipIds", juce::var(shuffled)}}));
            expect(joined.ok);
            expectEquals(static_cast<int>(joined.result["id"]), clipId);
            expectWithinAbsoluteError(static_cast<double>(joined.result["lengthBeats"]), 4.0, 1e-6);
            expectWithinAbsoluteError(
                static_cast<double>(joined.result["audio"]["sourceLengthSeconds"]), 2.0, 1e-6);
            expectWithinAbsoluteError(static_cast<double>(joined.result["audio"]["fadeOutSeconds"]),
                                      0.1, 1e-9);
            if (const auto* clip = ClipManager::getInstance().getClip(static_cast<ClipId>(clipId)))
                expectWithinAbsoluteError(clip->primaryEvent()->loopLengthSeconds(), 2.0, 1e-4);

            expect(UndoManager::getInstance().undo());
            expectEquals(static_cast<int>(ClipManager::getInstance()
                                              .getClipsOnTrack(static_cast<TrackId>(trackId),
                                                               ClipView::Arrangement)
                                              .size()),
                         3);
            const auto apart = fixture.run(
                "clips.join",
                object({{"clipIds", juce::var(juce::Array<juce::var>{pieceIds[0], pieceIds[2]})}}));
            expect(!apart.ok && apart.error.code == ErrorCode::Conflict);
            dir.deleteRecursively();
        }

        beginTest("Clips from different files join through a render job (#3345)");
        {
            Fixture fixture;
            FlatTempoMap tempo(120.0);
            fixture.api.setProjectTempoMap([&tempo] { return &tempo; });
            const auto engine = std::make_shared<SampleWritingEngineJobs>();
            fixture.service.setEngineJobSource(engine);
            const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("magda-render-join-test");
            dir.createDirectory();
            const auto trackId = static_cast<int>(
                fixture.run("tracks.create", object({{"name", "Voice"}, {"type", "audio"}}))
                    .result["id"]);
            const auto load = [&](const char* name, double beat) {
                const auto sample = writeSample(dir.getChildFile(name), 1.0);
                return static_cast<int>(
                    fixture
                        .run("clips.loadSample",
                             object({{"samplePath", sample.getFullPathName()},
                                     {"placement", arrangementDestination(trackId, beat)}}))
                        .result["id"]);
            };
            const auto leftId = load("first.wav", 0.0);
            const auto rightId = load("second.wav", 2.0);

            auto owner = fullyGrantedContext();
            owner.clientId = "join-client";
            const auto joined = fixture.run(
                "clips.join",
                object({{"clipIds", juce::var(juce::Array<juce::var>{rightId, leftId})}}), owner);
            expect(joined.ok, joined.error.message);
            expect(joined.result["kind"].toString() == "clips.join");
            expect(joined.result["state"].toString() == "completed");
            const auto& clip = joined.result["result"];
            expectWithinAbsoluteError(static_cast<double>(clip["startBeat"]), 0.0, 1e-9);
            expectWithinAbsoluteError(static_cast<double>(clip["lengthBeats"]), 4.0, 1e-6);
            const juce::File rendered(clip["audio"]["source"]["path"].toString());
            expect(rendered == engine->lastRequest.destination && rendered.existsAsFile());
            expect(!engine->lastRequest.usePlugins && !engine->lastRequest.useTrackEffects &&
                   !engine->lastRequest.useMasterPlugins);
            expect(engine->lastRequest.trackIds ==
                   std::vector<TrackId>{static_cast<TrackId>(trackId)});
            expectWithinAbsoluteError(engine->lastRequest.range.end.value, 4.0, 1e-9);

            const auto clipsOnTrack = [trackId] {
                return static_cast<int>(
                    ClipManager::getInstance()
                        .getClipsOnTrack(static_cast<TrackId>(trackId), ClipView::Arrangement)
                        .size());
            };
            expectEquals(clipsOnTrack(), 1);
            expect(UndoManager::getInstance().undo());
            expectEquals(clipsOnTrack(), 2);
            expect(ClipManager::getInstance().getClip(static_cast<ClipId>(leftId)) != nullptr);
            dir.deleteRecursively();
        }

        beginTest("A vocal clip's phrases come back in timeline beats (#3444)");
        {
            Fixture fixture;
            FlatTempoMap tempo(120.0);
            fixture.api.setProjectTempoMap([&tempo] { return &tempo; });
            fixture.service.setEngineJobSource(std::make_shared<SampleWritingEngineJobs>());
            const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("magda-speech-analysis-test");
            dir.createDirectory();
            const auto sample = writeVoice(dir.getChildFile("voice.wav"));
            const auto trackId = static_cast<int>(
                fixture.run("tracks.create", object({{"name", "Voice"}, {"type", "audio"}}))
                    .result["id"]);
            const auto clipId = static_cast<int>(
                fixture
                    .run("clips.loadSample",
                         object({{"samplePath", sample.getFullPathName()},
                                 {"placement", arrangementDestination(trackId, 4.0)}}))
                    .result["id"]);

            auto owner = fullyGrantedContext();
            owner.clientId = "speech-client";
            // The voice plays from 1 s to 2 s of the file: beats 6 to 8 at 120 BPM.
            const auto analysed =
                fixture.run("clips.analyzeSpeech",
                            object({{"clipId", clipId}, {"targetLoudness", -16.0}}), owner);
            expect(analysed.ok, analysed.error.message);
            expect(analysed.result["state"].toString() == "completed",
                   analysed.result["state"].toString());
            const auto* phrases = analysed.result["result"]["phrases"].getArray();
            expect(phrases != nullptr && phrases->size() == 1,
                   juce::JSON::toString(analysed.result["result"], true));
            if (phrases != nullptr && phrases->size() == 1) {
                const auto& phrase = phrases->getReference(0);
                expectWithinAbsoluteError(static_cast<double>(phrase["startBeat"]), 6.0, 0.1);
                expectWithinAbsoluteError(static_cast<double>(phrase["endBeat"]), 8.0, 0.1);
                expectWithinAbsoluteError(static_cast<double>(phrase["gainToTargetDb"]),
                                          -16.0 - static_cast<double>(phrase["loudness"]), 0.11);
            }

            const auto later = fixture.run("clips.analyzeSpeech",
                                           object({{"clipId", clipId}, {"startBeat", 7.0}}), owner);
            const auto* trimmed = later.result["result"]["phrases"].getArray();
            expect(trimmed != nullptr && trimmed->size() == 1);
            if (trimmed != nullptr && trimmed->size() == 1)
                expectWithinAbsoluteError(
                    static_cast<double>(trimmed->getReference(0)["startBeat"]), 7.0, 1e-6);

            // Each phrase is transcribed from its own audio (#3445).
            double heardSeconds = 0.0;
            setSpeechToText([&heardSeconds](const std::vector<float>& samples, double rate,
                                            const std::atomic<bool>*) {
                heardSeconds += static_cast<double>(samples.size()) / rate;
                return Transcription{" hello there ", {}};
            });
            const auto spoken = fixture.run(
                "clips.analyzeSpeech", object({{"clipId", clipId}, {"transcribe", true}}), owner);
            const auto* named = spoken.result["result"]["phrases"].getArray();
            expect(named != nullptr && named->size() == 1,
                   juce::JSON::toString(spoken.result, true));
            if (named != nullptr && named->size() == 1)
                expect(named->getReference(0)["text"].toString() == "hello there");
            expectWithinAbsoluteError(heardSeconds, 1.0, 0.1);

            setSpeechToText([](const std::vector<float>&, double, const std::atomic<bool>*) {
                return Transcription{{}, "The speech model is not downloaded."};
            });
            const auto unnamed = fixture.run(
                "clips.analyzeSpeech", object({{"clipId", clipId}, {"transcribe", true}}), owner);
            expect(unnamed.result["result"]["transcriptionError"].toString().isNotEmpty());
            const auto* kept = unnamed.result["result"]["phrases"].getArray();
            expect(kept != nullptr && kept->size() == 1 && kept->getReference(0)["text"].isVoid());

            setSpeechToText({});
            const auto missing = fixture.run(
                "clips.analyzeSpeech", object({{"clipId", clipId}, {"transcribe", true}}), owner);
            expect(!missing.ok && missing.error.code == ErrorCode::Conflict);

            expect(fixture.run("clips.updateAudio", object({{"clipId", clipId}, {"looping", true}}))
                       .ok);
            const auto looped =
                fixture.run("clips.analyzeSpeech", object({{"clipId", clipId}}), owner);
            expect(!looped.ok && looped.error.code == ErrorCode::Conflict);
            dir.deleteRecursively();
        }

        beginTest("An agent's call that starts a job returns the finished job (#3445)");
        {
            Fixture fixture;
            FlatTempoMap tempo(120.0);
            fixture.api.setProjectTempoMap([&tempo] { return &tempo; });
            const auto engine = std::make_shared<SampleWritingEngineJobs>();
            engine->deferFileTasks = true;
            fixture.service.setEngineJobSource(engine);
            const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("magda-agent-job-test");
            dir.createDirectory();
            const auto sample = writeVoice(dir.getChildFile("voice.wav"));
            const auto trackId = static_cast<int>(
                fixture.run("tracks.create", object({{"name", "Voice"}, {"type", "audio"}}))
                    .result["id"]);
            const auto clipId = static_cast<int>(
                fixture
                    .run("clips.loadSample",
                         object({{"samplePath", sample.getFullPathName()},
                                 {"placement", arrangementDestination(trackId, 4.0)}}))
                    .result["id"]);

            magda::agent::RemoteAgentToolExecutor executor(fixture.service, "@vocal",
                                                           {"clips.analyzeSpeech"});
            magda::agent::ToolResult result;
            std::atomic<bool> finished{false};
            std::thread agent([&] {
                result = executor.execute(
                    {.call = {.id = "call_0",
                              .name = "clips.analyzeSpeech",
                              .arguments = object({{"clipId", clipId}})},
                     .expectedRevision = 0,
                     .permissions = {},
                     .deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20)},
                    {});
                finished = true;
            });
            auto* messages = juce::MessageManager::getInstance();
            while (!engine->pendingFileTask)
                messages->runDispatchLoopUntil(10);
            // Long enough for several polls, each of which the old loop spent a model step on.
            messages->runDispatchLoopUntil(800);
            expect(!finished.load());
            engine->pendingFileTask();
            while (!finished.load())
                messages->runDispatchLoopUntil(10);
            agent.join();

            expect(result.success, result.error ? result.error->message : juce::String());
            expect(result.content["state"].toString() == "completed",
                   juce::JSON::toString(result.content, true));
            const auto* phrases = result.content["result"]["phrases"].getArray();
            expect(phrases != nullptr && phrases->size() == 1);
            dir.deleteRecursively();
        }

        beginTest("Two audio clips that meet take a crossfade as one undo step");
        {
            Fixture fixture;
            FlatTempoMap tempo(120.0);
            fixture.api.setProjectTempoMap([&tempo] { return &tempo; });
            const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("magda-crossfade-test");
            dir.createDirectory();
            const auto sample = writeSample(dir.getChildFile("voice.wav"), 2.0);
            const auto trackId = static_cast<int>(
                fixture.run("tracks.create", object({{"name", "Voice"}, {"type", "audio"}}))
                    .result["id"]);
            const auto load = [&](double beat) {
                return static_cast<int>(
                    fixture
                        .run("clips.loadSample",
                             object({{"samplePath", sample.getFullPathName()},
                                     {"placement", arrangementDestination(trackId, beat)}}))
                        .result["id"]);
            };
            // Each clip keeps a beat of file past the joint to fade over.
            const auto leftId = load(0.0);
            expect(fixture
                       .run("clips.resize",
                            object({{"clipId", leftId}, {"lengthBeats", 3.0}, {"edge", "end"}}))
                       .ok);
            const auto rightId = load(4.0);
            expect(fixture
                       .run("clips.resize",
                            object({{"clipId", rightId}, {"lengthBeats", 3.0}, {"edge", "start"}}))
                       .ok);
            expect(fixture
                       .run("clips.move",
                            object({{"clipId", rightId},
                                    {"destination", arrangementDestination(trackId, 3.0)}}))
                       .ok);

            const auto revision = fixture.service.currentRevision();
            const auto region = object({{"leftClipId", rightId},
                                        {"rightClipId", leftId},
                                        {"startBeat", 2.5},
                                        {"endBeat", 3.5}});
            const auto faded = fixture.run("clips.setCrossfade", region);
            expect(faded.ok);
            expect(fixture.service.currentRevision() == revision + 1);
            const auto* clips = faded.result.getArray();
            expect(clips != nullptr && clips->size() == 2);
            if (clips != nullptr && clips->size() == 2) {
                const auto& left = clips->getReference(0);
                const auto& right = clips->getReference(1);
                expectEquals(static_cast<int>(left["id"]), leftId);
                expectWithinAbsoluteError(static_cast<double>(left["lengthBeats"]), 3.5, 1e-6);
                expectWithinAbsoluteError(static_cast<double>(right["startBeat"]), 2.5, 1e-6);
                expect(static_cast<bool>(left["audio"]["autoCrossfade"]));
                expect(static_cast<bool>(right["audio"]["autoCrossfade"]));
            }
            expect(fixture.run("clips.setCrossfade", region).ok);
            expect(fixture.service.currentRevision() == revision + 1,
                   "Restating the overlap changes nothing");

            expect(UndoManager::getInstance().undo());
            const auto* left = ClipManager::getInstance().getClip(static_cast<ClipId>(leftId));
            const auto* right = ClipManager::getInstance().getClip(static_cast<ClipId>(rightId));
            expect(left != nullptr && right != nullptr);
            if (left != nullptr && right != nullptr) {
                expectWithinAbsoluteError(left->placement.endBeat(), 3.0, 1e-6);
                expectWithinAbsoluteError(right->placement.startBeat, 3.0, 1e-6);
            }

            const auto off = fixture.run("clips.updateAudio",
                                         object({{"clipId", leftId}, {"autoCrossfade", false}}));
            expect(off.ok && !static_cast<bool>(off.result["audio"]["autoCrossfade"]));

            const auto midiId = static_cast<int>(
                fixture
                    .run("clips.createMidi",
                         object({{"lengthBeats", 4.0},
                                 {"placement", arrangementDestination(trackId, 12.0)}}))
                    .result["id"]);
            const auto before = fixture.service.currentRevision();
            const auto apart = fixture.run("clips.setCrossfade", object({{"leftClipId", leftId},
                                                                         {"rightClipId", midiId},
                                                                         {"startBeat", 2.5},
                                                                         {"endBeat", 3.5}}));
            expect(!apart.ok && apart.error.code == ErrorCode::Conflict);
            const auto backwards =
                fixture.run("clips.setCrossfade", object({{"leftClipId", leftId},
                                                          {"rightClipId", rightId},
                                                          {"startBeat", 3.5},
                                                          {"endBeat", 2.5}}));
            expect(!backwards.ok && backwards.error.code == ErrorCode::ValidationFailed);
            expect(fixture.service.currentRevision() == before);
            dir.deleteRecursively();
        }

        beginTest("Arrangement clips move, resize, and duplicate through undoable operations");
        {
            Fixture fixture;
            const auto first =
                fixture.run("tracks.create", object({{"name", "First"}, {"type", "audio"}}));
            const auto second =
                fixture.run("tracks.create", object({{"name", "Second"}, {"type", "audio"}}));
            expect(first.ok && second.ok);
            const auto firstId = static_cast<TrackId>(static_cast<int>(first.result["id"]));
            const auto secondId = static_cast<TrackId>(static_cast<int>(second.result["id"]));
            const auto created = fixture.run(
                "clips.createMidi", object({{"lengthBeats", 4.0},
                                            {"placement", arrangementDestination(firstId, 0.0)}}));
            expect(created.ok);
            const auto clipId = static_cast<ClipId>(static_cast<int>(created.result["id"]));

            const auto beforeMove = fixture.service.currentRevision();
            const auto moved = fixture.run(
                "clips.move", object({{"clipId", static_cast<int>(clipId)},
                                      {"destination", arrangementDestination(secondId, 8.0)}}));
            expect(moved.ok);
            expect(static_cast<int>(moved.result["trackId"]) == secondId);
            expect(static_cast<double>(moved.result["startBeat"]) == 8.0);
            expect(fixture.service.currentRevision() == beforeMove + 1);

            // Track and time changed through two commands, but the request is
            // one compound and therefore one Undo.
            expect(UndoManager::getInstance().undo());
            const auto* restored = ClipManager::getInstance().getClip(clipId);
            expect(restored != nullptr);
            if (restored != nullptr) {
                expect(restored->trackId == firstId);
                expect(restored->placement.startBeat == 0.0);
            }

            const auto beforeNoOp = fixture.service.currentRevision();
            expect(fixture
                       .run("clips.move",
                            object({{"clipId", static_cast<int>(clipId)},
                                    {"destination", arrangementDestination(firstId, 0.0)}}))
                       .ok);
            expect(fixture.service.currentRevision() == beforeNoOp);

            const auto resized =
                fixture.run("clips.resize", object({{"clipId", static_cast<int>(clipId)},
                                                    {"lengthBeats", 2.0},
                                                    {"edge", "end"}}));
            expect(resized.ok);
            expect(static_cast<double>(resized.result["lengthBeats"]) == 2.0);
            expect(UndoManager::getInstance().undo());
            expect(ClipManager::getInstance().getClip(clipId)->placement.lengthBeats == 4.0);

            const auto duplicated =
                fixture.run("clips.duplicate",
                            object({{"clipId", static_cast<int>(clipId)},
                                    {"destination", arrangementDestination(secondId, 12.0)}}));
            expect(duplicated.ok);
            const auto duplicateId = static_cast<ClipId>(static_cast<int>(duplicated.result["id"]));
            expect(duplicateId != INVALID_CLIP_ID && duplicateId != clipId);
            expect(ClipManager::getInstance().getClip(duplicateId) != nullptr);
            expect(UndoManager::getInstance().undo());
            expect(ClipManager::getInstance().getClip(duplicateId) == nullptr);
        }

        beginTest("Session placement addresses stable scenes and applies occupied-slot policy");
        {
            Fixture fixture;
            const auto first =
                fixture.run("tracks.create", object({{"name", "First"}, {"type", "audio"}}));
            const auto second =
                fixture.run("tracks.create", object({{"name", "Second"}, {"type", "audio"}}));
            expect(first.ok && second.ok);
            const auto firstId = static_cast<TrackId>(static_cast<int>(first.result["id"]));
            const auto secondId = static_cast<TrackId>(static_cast<int>(second.result["id"]));
            const auto firstScene = sceneIdAt(0);
            const auto secondScene = sceneIdAt(1);

            const auto created =
                fixture.run("clips.createMidi",
                            object({{"lengthBeats", 4.0},
                                    {"placement", sessionDestination(firstId, firstScene)}}));
            const auto blocker =
                fixture.run("clips.createMidi",
                            object({{"lengthBeats", 8.0},
                                    {"placement", sessionDestination(secondId, secondScene)}}));
            expect(created.ok && blocker.ok);
            const auto clipId = static_cast<ClipId>(static_cast<int>(created.result["id"]));
            const auto blockerId = static_cast<ClipId>(static_cast<int>(blocker.result["id"]));
            expect(ClipManager::getInstance().getClipInSlot(firstId, 0) == clipId);
            expect(ClipManager::getInstance().getClipInSlot(secondId, 1) == blockerId);

            const auto beforeNoOp = fixture.service.currentRevision();
            expect(fixture
                       .run("clips.move",
                            object({{"clipId", static_cast<int>(clipId)},
                                    {"destination", sessionDestination(firstId, firstScene)}}))
                       .ok);
            expect(fixture.service.currentRevision() == beforeNoOp);

            const auto beforeConflict = fixture.service.currentRevision();
            const auto occupied = fixture.run(
                "clips.move", object({{"clipId", static_cast<int>(clipId)},
                                      {"destination", sessionDestination(secondId, secondScene)}}));
            expect(!occupied.ok);
            expectEquals(toString(occupied.error.code), juce::String("conflict"));
            expect(fixture.service.currentRevision() == beforeConflict);

            const auto swapped = fixture.run(
                "clips.move",
                object({{"clipId", static_cast<int>(clipId)},
                        {"destination", sessionDestination(secondId, secondScene, "swap")}}));
            expect(swapped.ok);
            expect(ClipManager::getInstance().getClipInSlot(secondId, 1) == clipId);
            expect(ClipManager::getInstance().getClipInSlot(firstId, 0) == blockerId);
            expect(UndoManager::getInstance().undo());
            expect(ClipManager::getInstance().getClipInSlot(firstId, 0) == clipId);
            expect(ClipManager::getInstance().getClipInSlot(secondId, 1) == blockerId);
            expect(UndoManager::getInstance().redo());
            expect(ClipManager::getInstance().getClipInSlot(secondId, 1) == clipId);
            expect(ClipManager::getInstance().getClipInSlot(firstId, 0) == blockerId);

            const auto duplicated = fixture.run(
                "clips.duplicate",
                object({{"clipId", static_cast<int>(clipId)},
                        {"destination", sessionDestination(firstId, firstScene, "replace")}}));
            expect(duplicated.ok);
            const auto duplicateId = static_cast<ClipId>(static_cast<int>(duplicated.result["id"]));
            expect(ClipManager::getInstance().getClip(blockerId) == nullptr);
            expect(ClipManager::getInstance().getClipInSlot(firstId, 0) == duplicateId);
            expect(UndoManager::getInstance().undo());
            expect(ClipManager::getInstance().getClip(blockerId) != nullptr);
            expect(ClipManager::getInstance().getClip(duplicateId) == nullptr);
            expect(UndoManager::getInstance().redo());
            expect(ClipManager::getInstance().getClipInSlot(firstId, 0) == duplicateId);
            expect(ClipManager::getInstance().getClip(blockerId) == nullptr);

            const auto replacement = fixture.run(
                "clips.createMidi",
                object({{"lengthBeats", 2.0},
                        {"placement", sessionDestination(firstId, firstScene, "replace")}}));
            expect(replacement.ok);
            const auto replacementId =
                static_cast<ClipId>(static_cast<int>(replacement.result["id"]));
            expect(ClipManager::getInstance().getClip(duplicateId) == nullptr);
            expect(ClipManager::getInstance().getClipInSlot(firstId, 0) == replacementId);
            expect(UndoManager::getInstance().undo());
            expect(ClipManager::getInstance().getClipInSlot(firstId, 0) == duplicateId);
            expect(UndoManager::getInstance().redo());
            expect(ClipManager::getInstance().getClipInSlot(firstId, 0) == replacementId);

            const auto legacyId = ClipManager::getInstance().createMidiClipBeats(firstId, 0.0, 4.0,
                                                                                 ClipView::Session);
            expect(ClipManager::getInstance().getClip(legacyId)->sceneIndex == -1);
            const auto beforeLegacy = fixture.service.currentRevision();
            const auto legacyMove =
                fixture.run("clips.move",
                            object({{"clipId", static_cast<int>(legacyId)},
                                    {"destination", sessionDestination(secondId, sceneIdAt(3))}}));
            expect(!legacyMove.ok);
            expectEquals(toString(legacyMove.error.code), juce::String("conflict"));
            expect(ClipManager::getInstance().getClip(legacyId)->sceneIndex == -1);
            expect(fixture.service.currentRevision() == beforeLegacy);

            const auto wrongView = fixture.run(
                "clips.move", object({{"clipId", static_cast<int>(clipId)},
                                      {"destination", arrangementDestination(secondId, 4.0)}}));
            expect(!wrongView.ok);
            expectEquals(toString(wrongView.error.code), juce::String("conflict"));
        }

        beginTest("Expressive MIDI events have atomic bulk CRUD and one-step undo");
        {
            Fixture fixture;
            const auto track =
                fixture.run("tracks.create", object({{"name", "Expressive"}, {"type", "audio"}}));
            expect(track.ok);
            const auto trackId = static_cast<int>(track.result["id"]);
            const auto clip = fixture.run(
                "clips.createMidi", object({{"lengthBeats", 4.0},
                                            {"placement", arrangementDestination(trackId, 0.0)}}));
            expect(clip.ok);
            const auto clipId = static_cast<int>(clip.result["id"]);

            juce::Array<juce::var> additions;
            additions.add(object({{"type", "note"},
                                  {"note", 36},
                                  {"velocity", 100},
                                  {"beat", 0.0},
                                  {"lengthBeats", 0.25},
                                  {"keyswitch", true}}));
            additions.add(object(
                {{"type", "controlChange"}, {"controller", 74}, {"value", 96}, {"beat", 0.5}}));
            additions.add(object({{"type", "pitchBend"}, {"value", 9000}, {"beat", 1.0}}));
            additions.add(object({{"type", "channelPressure"}, {"value", 80}, {"beat", 1.5}}));
            additions.add(
                object({{"type", "polyAftertouch"}, {"note", 60}, {"value", 70}, {"beat", 2.0}}));

            const auto added =
                fixture.run("clips.addMidiEvents",
                            object({{"clipId", clipId}, {"events", juce::var(additions)}}));
            expect(added.ok);
            auto* addedEvents = added.result["midiEvents"].getArray();
            expect(addedEvents != nullptr && addedEvents->size() == 5);
            if (addedEvents != nullptr && addedEvents->size() == 5) {
                std::vector<int> ids;
                for (const auto& event : *addedEvents)
                    ids.push_back(static_cast<int>(event["id"]));
                std::ranges::sort(ids);
                expect(std::ranges::adjacent_find(ids) == ids.end());
                expect(ids.front() > 0);
                const int oldMaximumId = ids.back();

                const auto listed =
                    fixture.run("clips.listMidiEvents", object({{"clipId", clipId}}));
                expect(listed.ok);
                expect(listed.result.getArray() != nullptr &&
                       listed.result.getArray()->size() == 5);

                const auto noteId = static_cast<int>((*addedEvents)[0]["id"]);
                juce::Array<juce::var> updates;
                updates.add(object({{"id", noteId},
                                    {"type", "note"},
                                    {"note", 38},
                                    {"velocity", 110},
                                    {"beat", 0.25},
                                    {"lengthBeats", 0.5},
                                    {"keyswitch", false}}));
                const auto updated =
                    fixture.run("clips.updateMidiEvents",
                                object({{"clipId", clipId}, {"events", juce::var(updates)}}));
                expect(updated.ok);
                expectEquals(static_cast<int>((*updated.result["midiEvents"].getArray())[0]["id"]),
                             noteId);

                const auto beforeInvalid = fixture.service.currentRevision();
                const auto stateBeforeInvalid =
                    ClipManager::getInstance().getClip(clipId)->midiEventState();
                juce::Array<juce::var> invalid;
                invalid.add(object(
                    {{"type", "controlChange"}, {"controller", 1}, {"value", 127}, {"beat", 0.0}}));
                invalid.add(object({{"type", "note"},
                                    {"note", 60},
                                    {"velocity", 100},
                                    {"beat", 3.75},
                                    {"lengthBeats", 1.0}}));
                const auto rejected =
                    fixture.run("clips.addMidiEvents",
                                object({{"clipId", clipId}, {"events", juce::var(invalid)}}));
                expect(!rejected.ok);
                expect(fixture.service.currentRevision() == beforeInvalid);
                expect(ClipManager::getInstance().getClip(clipId)->midiEventState() ==
                       stateBeforeInvalid);

                juce::Array<juce::var> deletedIds;
                deletedIds.add(ids[1]);
                deletedIds.add(ids[2]);
                const auto deleted =
                    fixture.run("clips.deleteMidiEvents",
                                object({{"clipId", clipId}, {"eventIds", juce::var(deletedIds)}}));
                expect(deleted.ok);
                expect(deleted.result["midiEvents"].getArray()->size() == 3);
                UndoManager::getInstance().undo();
                expect(ClipManager::getInstance().getClip(clipId)->midiEventState().cc.size() +
                           ClipManager::getInstance()
                               .getClip(clipId)
                               ->midiEventState()
                               .pitchBend.size() ==
                       2);

                juce::Array<juce::var> replacement;
                replacement.add(
                    object({{"type", "channelPressure"}, {"value", 64}, {"beat", 0.0}}));
                const auto replaced =
                    fixture.run("clips.replaceMidiEvents",
                                object({{"clipId", clipId}, {"events", juce::var(replacement)}}));
                expect(replaced.ok);
                auto* replacementEvents = replaced.result["midiEvents"].getArray();
                expect(replacementEvents != nullptr && replacementEvents->size() == 1);
                if (replacementEvents != nullptr && replacementEvents->size() == 1)
                    expect(static_cast<int>((*replacementEvents)[0]["id"]) > oldMaximumId);
            }
        }

        beginTest("Adding a point to a clip-based lane is refused");
        {
            Fixture fixture;
            const auto laneId = createLane(AutomationLaneType::ClipBased);
            expect(laneId != INVALID_AUTOMATION_LANE_ID);

            const auto before = fixture.service.currentRevision();
            const auto response =
                fixture.run("automation.addPoint", object({{"laneId", static_cast<int>(laneId)},
                                                           {"beatPosition", 0.0},
                                                           {"value", 0.5},
                                                           {"curve", "linear"}}));

            // Points on a clip-based lane live on its clips.
            expect(!response.ok);
            expectEquals(toString(response.error.code), juce::String("conflict"));
            expect(fixture.service.currentRevision() == before);
        }

        beginTest("Clearing an already-empty lane does not advance the revision");
        {
            Fixture fixture;
            const auto laneId = createLane(AutomationLaneType::Absolute);
            expect(laneId != INVALID_AUTOMATION_LANE_ID);

            // A fresh lane is not necessarily empty, so reach a known state
            // first. That clear is a real mutation.
            expect(
                fixture.run("automation.clearLane", object({{"laneId", static_cast<int>(laneId)}}))
                    .ok);

            const auto before = fixture.service.currentRevision();
            const auto response =
                fixture.run("automation.clearLane", object({{"laneId", static_cast<int>(laneId)}}));

            // Succeeds — the lane is in the requested state — but nothing
            // changed, so the revision holds.
            expect(response.ok);
            expect(fixture.service.currentRevision() == before);
        }

        beginTest("Replacing a lane curve is atomic, undoable, and no-op aware");
        {
            Fixture fixture;
            const auto laneId = createLane(AutomationLaneType::Absolute);
            expect(laneId != INVALID_AUTOMATION_LANE_ID);
            const auto original = AutomationManager::getInstance().getLane(laneId)->absolutePoints;

            juce::Array<juce::var> points;
            points.add(object({{"beatPosition", 8.0}, {"value", 0.8}, {"curve", "step"}}));
            points.add(object({{"beatPosition", 2.0}, {"value", 0.2}, {"curve", "linear"}}));
            const auto input =
                object({{"laneId", static_cast<int>(laneId)}, {"points", juce::var(points)}});

            const auto before = fixture.service.currentRevision();
            const auto replaced = fixture.run("automation.setPoints", input);
            expect(replaced.ok);
            expect(fixture.service.currentRevision() == before + 1);
            const auto* resultPoints = replaced.result["points"].getArray();
            expect(resultPoints != nullptr && resultPoints->size() == 2);
            if (resultPoints != nullptr && resultPoints->size() == 2) {
                expectWithinAbsoluteError(static_cast<double>((*resultPoints)[0]["beatPosition"]),
                                          2.0, 1.0e-9);
                expectWithinAbsoluteError(static_cast<double>((*resultPoints)[1]["beatPosition"]),
                                          8.0, 1.0e-9);
            }

            const auto unchanged = fixture.run("automation.setPoints", input);
            expect(unchanged.ok);
            expect(fixture.service.currentRevision() == before + 1);

            UndoManager::getInstance().undo();
            const auto* restored = AutomationManager::getInstance().getLane(laneId);
            expect(restored != nullptr);
            if (restored != nullptr) {
                expect(restored->absolutePoints.size() == original.size());
                if (!original.empty() && !restored->absolutePoints.empty())
                    expect(restored->absolutePoints.front().id == original.front().id);
            }
        }

        beginTest("Automation reads and writes in its target's units (#3348)");
        {
            Fixture fixture;
            const auto laneOf = [](ControlTarget::Kind kind) {
                AutomationTarget target;
                target.kind = kind;
                if (kind != ControlTarget::Kind::Tempo)
                    target.devicePath = ChainNodePath::trackLevel(
                        TrackManager::getInstance().createTrack("Units", TrackType::Media));
                return static_cast<int>(AutomationManager::getInstance().createLane(
                    target, AutomationLaneType::Absolute));
            };
            const auto write = [&fixture](int laneId, juce::var point) {
                juce::Array<juce::var> points;
                points.add(point);
                return fixture.run("automation.setPoints",
                                   object({{"laneId", laneId}, {"points", juce::var(points)}}));
            };
            const auto real = [](double value) {
                return object({{"beatPosition", 0.0}, {"realValue", value}, {"curve", "linear"}});
            };

            const auto volumeLane = laneOf(ControlTarget::Kind::TrackVolume);
            const auto volume = write(volumeLane, real(-6.0));
            expect(volume.ok);
            expectWithinAbsoluteError(static_cast<double>(volume.result["minValue"]), -60.0, 1e-6);
            expect(volume.result["scale"].toString() == "fader_db");
            expectWithinAbsoluteError(static_cast<double>(volume.result["points"][0]["realValue"]),
                                      -6.0, 1e-4);
            // Below unity the fader is linear in dB up to 0 dB at 0.75.
            expectWithinAbsoluteError(static_cast<double>(volume.result["points"][0]["value"]),
                                      0.675, 1e-6);

            const auto pan = write(laneOf(ControlTarget::Kind::TrackPan), real(-0.5));
            expect(pan.ok);
            expectWithinAbsoluteError(static_cast<double>(pan.result["points"][0]["value"]), 0.25,
                                      1e-6);
            const auto tempo = write(laneOf(ControlTarget::Kind::Tempo), real(140.0));
            expect(tempo.ok);
            expectWithinAbsoluteError(static_cast<double>(tempo.result["points"][0]["realValue"]),
                                      140.0, 1e-3);

            const auto before = fixture.service.currentRevision();
            const auto loud = write(volumeLane, real(12.0));
            expect(!loud.ok && loud.error.code == ErrorCode::ValidationFailed);
            const auto both = write(volumeLane, object({{"beatPosition", 0.0},
                                                        {"value", 0.5},
                                                        {"realValue", 0.0},
                                                        {"curve", "linear"}}));
            expect(!both.ok && both.error.code == ErrorCode::ValidationFailed);
            expect(fixture.service.currentRevision() == before);
        }

        beginTest("Deleting an automation lane restores its clips on undo");
        {
            Fixture fixture;
            const auto laneId = createLane(AutomationLaneType::ClipBased);
            const auto clipId = AutomationManager::getInstance().createClip(laneId, 4.0, 8.0);
            expect(clipId != INVALID_AUTOMATION_CLIP_ID);

            const auto deleted = fixture.run("automation.deleteLane",
                                             object({{"laneId", static_cast<int>(laneId)}}));
            expect(deleted.ok);
            expect(AutomationManager::getInstance().getLane(laneId) == nullptr);
            expect(AutomationManager::getInstance().getClip(clipId) == nullptr);

            UndoManager::getInstance().undo();
            expect(AutomationManager::getInstance().getLane(laneId) != nullptr);
            expect(AutomationManager::getInstance().getClip(clipId) != nullptr);
        }

        beginTest("Automation clips support safe reads and undoable lifecycle edits");
        {
            Fixture fixture;
            const auto laneId = createLane(AutomationLaneType::ClipBased);
            const auto created =
                fixture.run("automation.createClip", object({{"laneId", static_cast<int>(laneId)},
                                                             {"startBeat", 4.0},
                                                             {"lengthBeats", 8.0}}));
            expect(created.ok);
            const auto clipId =
                static_cast<AutomationClipId>(static_cast<int>(created.result["id"]));
            expect(clipId != INVALID_AUTOMATION_CLIP_ID);
            expect(created.result["points"].getArray()->size() == 2);

            const auto listed =
                fixture.run("automation.listClips", object({{"laneId", static_cast<int>(laneId)}}));
            expect(listed.ok);
            expect(listed.result.getArray()->size() == 1);
            const auto fetched =
                fixture.run("automation.getClip", object({{"clipId", static_cast<int>(clipId)}}));
            expect(fetched.ok);
            expectEquals(fetched.result["name"].toString(), created.result["name"].toString());

            const auto moved =
                fixture.run("automation.moveClip",
                            object({{"clipId", static_cast<int>(clipId)}, {"startBeat", 12.0}}));
            expect(moved.ok);
            expectWithinAbsoluteError(static_cast<double>(moved.result["startBeat"]), 12.0, 1.0e-9);
            UndoManager::getInstance().undo();
            expectWithinAbsoluteError(AutomationManager::getInstance().getClip(clipId)->startBeats,
                                      4.0, 1.0e-9);

            const auto resized =
                fixture.run("automation.resizeClip", object({{"clipId", static_cast<int>(clipId)},
                                                             {"lengthBeats", 6.0},
                                                             {"edge", "end"}}));
            expect(resized.ok);
            expectWithinAbsoluteError(static_cast<double>(resized.result["lengthBeats"]), 6.0,
                                      1.0e-9);
            UndoManager::getInstance().undo();
            expectWithinAbsoluteError(AutomationManager::getInstance().getClip(clipId)->lengthBeats,
                                      8.0, 1.0e-9);

            const auto duplicated = fixture.run("automation.duplicateClip",
                                                object({{"clipId", static_cast<int>(clipId)}}));
            expect(duplicated.ok);
            const auto duplicateId =
                static_cast<AutomationClipId>(static_cast<int>(duplicated.result["id"]));
            expect(duplicateId != clipId);
            expectWithinAbsoluteError(static_cast<double>(duplicated.result["startBeat"]), 12.0,
                                      1.0e-9);
            UndoManager::getInstance().undo();
            expect(AutomationManager::getInstance().getClip(duplicateId) == nullptr);

            juce::Array<juce::var> points;
            points.add(object({{"beatPosition", 0.0}, {"value", 0.2}, {"curve", "linear"}}));
            points.add(object({{"beatPosition", 4.0}, {"value", 0.8}, {"curve", "step"}}));
            const auto updateInput = object({{"clipId", static_cast<int>(clipId)},
                                             {"name", "Remote curve"},
                                             {"colourArgb", static_cast<juce::int64>(0xFF123456)},
                                             {"looping", true},
                                             {"loopLengthBeats", 4.0},
                                             {"points", juce::var(points)}});
            const auto beforeUpdate = fixture.service.currentRevision();
            const auto updated = fixture.run("automation.updateClip", updateInput);
            expect(updated.ok);
            expectEquals(updated.result["name"].toString(), juce::String("Remote curve"));
            expect(static_cast<bool>(updated.result["looping"]));
            expect(updated.result["points"].getArray()->size() == 2);
            expect(fixture.service.currentRevision() == beforeUpdate + 1);

            const auto unchanged = fixture.run("automation.updateClip", updateInput);
            expect(unchanged.ok);
            expect(fixture.service.currentRevision() == beforeUpdate + 1);

            juce::Array<juce::var> invalidPoints;
            invalidPoints.add(object({{"beatPosition", 9.0}, {"value", 0.5}, {"curve", "linear"}}));
            const auto rejected = fixture.run("automation.updateClip",
                                              object({{"clipId", static_cast<int>(clipId)},
                                                      {"points", juce::var(invalidPoints)}}));
            expect(!rejected.ok);
            expectEquals(toString(rejected.error.code), juce::String("validation_failed"));
            expect(fixture.service.currentRevision() == beforeUpdate + 1);

            UndoManager::getInstance().undo();
            const auto* restored = AutomationManager::getInstance().getClip(clipId);
            expect(restored != nullptr);
            if (restored != nullptr) {
                expect(restored->name != "Remote curve");
                expect(!restored->looping);
            }

            const auto deleted = fixture.run("automation.deleteClip",
                                             object({{"clipId", static_cast<int>(clipId)}}));
            expect(deleted.ok);
            expect(AutomationManager::getInstance().getClip(clipId) == nullptr);
            UndoManager::getInstance().undo();
            expect(AutomationManager::getInstance().getClip(clipId) != nullptr);
        }

        beginTest("A lane targeting a track that does not exist is refused");
        {
            Fixture fixture;
            auto* path = new juce::DynamicObject();
            path->setProperty("trackId", 9999);
            path->setProperty("section", "fx");
            path->setProperty("trackLevel", true);
            path->setProperty("topLevelDeviceId", juce::var());
            path->setProperty("steps", juce::Array<juce::var>{});

            auto* target = new juce::DynamicObject();
            target->setProperty("kind", "track_volume");
            target->setProperty("devicePath", juce::var(path));
            target->setProperty("parameterIndex", -1);
            target->setProperty("modId", -1);
            target->setProperty("modParameterIndex", -1);
            target->setProperty("sendBusIndex", -1);

            const auto response =
                fixture.run("automation.createLane",
                            object({{"target", juce::var(target)}, {"type", "absolute"}}));

            // The shape is valid, but the track it names does not exist, so the
            // lane would be created against nothing.
            expect(!response.ok);
            expectEquals(toString(response.error.code), juce::String("not_found"));
        }

        beginTest("A lane targeting a device that does not exist is refused");
        {
            Fixture fixture;
            const auto trackId =
                TrackManager::getInstance().createTrack("Device host", TrackType::Media);

            auto* path = new juce::DynamicObject();
            path->setProperty("trackId", static_cast<int>(trackId));
            path->setProperty("section", "fx");
            path->setProperty("trackLevel", false);
            path->setProperty("topLevelDeviceId", 9999);
            path->setProperty("steps", juce::Array<juce::var>{});

            auto* target = new juce::DynamicObject();
            target->setProperty("kind", "plugin_param");
            target->setProperty("devicePath", juce::var(path));
            target->setProperty("parameterIndex", 0);
            target->setProperty("modId", -1);
            target->setProperty("modParameterIndex", -1);
            target->setProperty("sendBusIndex", -1);

            const auto response =
                fixture.run("automation.createLane",
                            object({{"target", juce::var(target)}, {"type", "absolute"}}));

            expect(!response.ok);
            expectEquals(toString(response.error.code), juce::String("not_found"));
        }

        beginTest("A read is not rejected by a stale expected revision");
        {
            Fixture fixture;
            const auto stale = fixture.service.currentRevision();
            expect(
                fixture.run("tracks.create", object({{"name", "Moved on"}, {"type", "audio"}})).ok);

            auto context = fullyGrantedContext();
            context.expectedRevision = stale;
            // A read is safe at any revision; only writes are gated.
            const auto response = fixture.run("tracks.list", object({}), context);
            expect(response.ok);
        }

        beginTest("A batch builds content as one undo step and rolls back in full on failure");
        {
            Fixture fixture;
            const auto ref = [](const char* text) { return object({{"$ref", text}}); };
            const auto step = [](const char* method, juce::var params) {
                return object({{"method", method}, {"params", std::move(params)}});
            };
            juce::Array<juce::var> events{object({{"type", "note"},
                                                  {"note", 60},
                                                  {"velocity", 100},
                                                  {"beat", 0.0},
                                                  {"lengthBeats", 1.0}})};
            juce::Array<juce::var> steps{
                step("tracks.create", object({{"name", "Bass"}, {"type", "audio"}})),
                step("clips.createMidi", object({{"lengthBeats", 4.0},
                                                 {"placement", object({{"view", "arrangement"},
                                                                       {"trackId", ref("0.id")},
                                                                       {"startBeat", 0.0}})}})),
                step("clips.addMidiEvents", object({{"clipId", ref("1.id")}, {"events", events}})),
                step("tracks.update", object({{"trackId", ref("0.id")}, {"volume", 0.5}})),
                step("project.setLoopRange", object({{"startBeat", 0.0}, {"endBeat", 4.0}}))};

            auto failing = steps;
            failing.add(step("clips.update", object({{"clipId", 999999}, {"name", "Gone"}})));
            const auto before = fixture.service.currentRevision();
            const auto loopBefore =
                ProjectManager::getInstance().getCurrentProjectInfo().loopEndBeats;
            const auto refused = fixture.run("batch.apply", object({{"steps", failing}}));
            expect(!refused.ok);
            expect(refused.error.message.startsWith("step 5 (clips.update)"));
            expect(TrackManager::getInstance().getTracks().empty());
            expect(ClipManager::getInstance().getClips().empty());
            expectEquals(ProjectManager::getInstance().getCurrentProjectInfo().loopEndBeats,
                         loopBefore);
            expect(!UndoManager::getInstance().canUndo());
            expectEquals(static_cast<juce::int64>(fixture.service.currentRevision()),
                         static_cast<juce::int64>(before));

            const auto applied = fixture.run("batch.apply", object({{"steps", steps}}));
            expect(applied.ok);
            expectEquals(static_cast<juce::int64>(applied.revision),
                         static_cast<juce::int64>(before + 1));
            expectEquals(static_cast<int>(TrackManager::getInstance().getTracks().size()), 1);
            const auto clipId = static_cast<int>(applied.result["results"][1]["id"]);
            const auto* clip = ClipManager::getInstance().getClip(clipId);
            expect(clip != nullptr && clip->midiNotes.size() == 1);

            expect(UndoManager::getInstance().undo());
            expect(TrackManager::getInstance().getTracks().empty());
            expect(ClipManager::getInstance().getClips().empty());
            expect(!UndoManager::getInstance().canUndo());
        }

        beginTest("A batch splits a vocal, levels and deletes pieces by index, as one undo step "
                  "(#3445)");
        {
            Fixture fixture;
            FlatTempoMap tempo(120.0);
            fixture.api.setProjectTempoMap([&tempo] { return &tempo; });
            const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("magda-batch-level-test");
            dir.createDirectory();
            const auto sample = writeSample(dir.getChildFile("voice.wav"), 2.0);
            const auto trackId = static_cast<int>(
                fixture.run("tracks.create", object({{"name", "Voice"}, {"type", "audio"}}))
                    .result["id"]);
            const auto clipId = static_cast<int>(
                fixture
                    .run("clips.loadSample",
                         object({{"samplePath", sample.getFullPathName()},
                                 {"placement", arrangementDestination(trackId, 0.0)}}))
                    .result["id"]);
            const auto clipsOnTrack = [trackId] {
                return static_cast<int>(
                    ClipManager::getInstance()
                        .getClipsOnTrack(static_cast<TrackId>(trackId), ClipView::Arrangement)
                        .size());
            };
            const auto ref = [](const char* text) { return object({{"$ref", text}}); };
            const auto step = [](const char* method, juce::var params) {
                return object({{"method", method}, {"params", std::move(params)}});
            };
            juce::Array<juce::var> steps{
                step("clips.split",
                     object({{"clipId", clipId},
                             {"beats", juce::var(juce::Array<juce::var>{1.0, 3.0})}})),
                step("clips.updateAudio", object({{"clipId", ref("0.1.id")},
                                                  {"gainDb", 6.0},
                                                  {"fadeInSeconds", 0.01},
                                                  {"fadeOutSeconds", 0.01}})),
                step("clips.delete", object({{"clipId", ref("0.2.id")}}))};

            auto failing = steps;
            failing.add(step("clips.delete", object({{"clipId", ref("0.5.id")}})));
            const auto refused = fixture.run("batch.apply", object({{"steps", failing}}));
            expect(!refused.ok && refused.error.code == ErrorCode::NotFound);
            expectEquals(clipsOnTrack(), 1);

            const auto applied = fixture.run("batch.apply", object({{"steps", steps}}));
            expect(applied.ok);
            expectEquals(clipsOnTrack(), 2);
            const auto levelled =
                static_cast<ClipId>(static_cast<int>(applied.result["results"][0]["ids"][1]));
            if (const auto* clip = ClipManager::getInstance().getClip(levelled)) {
                expectWithinAbsoluteError(clip->gainDB, 6.0f, 1e-6f);
                expectWithinAbsoluteError(clip->primaryEvent()->fadeInSeconds, 0.01, 1e-9);
            } else {
                expect(false, "the levelled piece is missing");
            }

            expect(UndoManager::getInstance().undo());
            expectEquals(clipsOnTrack(), 1);
            dir.deleteRecursively();
        }
    }

  private:
    /// Owns the service and bridge and resets the shared singletons around each
    /// section, so sections cannot leak state into one another.
    struct Fixture {
        MagdaApiLive api;
        RemoteApiService service{api};
        std::unique_ptr<ModelChangeBridge> bridge;

        Fixture() {
            reset();
            bridge = std::make_unique<ModelChangeBridge>(service);
        }

        ~Fixture() {
            bridge.reset();
            reset();
        }

        Fixture(const Fixture&) = delete;
        Fixture& operator=(const Fixture&) = delete;

        static void reset() {
            AutomationManager::getInstance().clearAll();
            ClipManager::getInstance().clearAllClips();
            TrackManager::getInstance().clearAllTracks();
            UndoManager::getInstance().clearHistory();
            SelectionManager::getInstance().clearSelection();
        }

        Response run(const juce::String& name, const juce::var& input,
                     RequestContext context = fullyGrantedContext()) {
            Response captured;
            service.dispatch(name, input, context,
                             [&captured](Response response) { captured = std::move(response); });
            return captured;
        }

        std::optional<RemoteJobDto> waitForJob(const juce::String& id,
                                               const RequestContext& request) {
            for (int attempt = 0; attempt < 200; ++attempt) {
                Error error;
                auto job = service.jobs().get(id, request.clientId, request.scopes, error);
                if (job && isTerminal(job->state))
                    return job;
                juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            }
            return std::nullopt;
        }
    };

    static AutomationLaneId createLane(AutomationLaneType type) {
        const auto trackId = TrackManager::getInstance().createTrack("Auto", TrackType::Media);
        AutomationTarget target;
        target.kind = ControlTarget::Kind::TrackVolume;
        target.devicePath = ChainNodePath::trackLevel(trackId);
        return AutomationManager::getInstance().createLane(target, type);
    }

    static bool hasTopic(const std::vector<ChangeSource::Change>& changes, Topic topic) {
        return std::any_of(changes.begin(), changes.end(),
                           [topic](const auto& change) { return change.topic == topic; });
    }
};

RemoteServiceLiveTest remoteServiceLiveTest;

}  // namespace
