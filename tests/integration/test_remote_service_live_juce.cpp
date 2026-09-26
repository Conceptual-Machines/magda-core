#include <juce_core/juce_core.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "RemoteTestScopes.hpp"
#include "magda/daw/api/magda_api_live.hpp"
#include "magda/daw/api/remote_model_bridge.hpp"
#include "magda/daw/api/remote_service.hpp"
#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"
#include "magda/daw/project/ProjectManager.hpp"

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

juce::var arrangementDestination(TrackId trackId, double startBeat) {
    return object({{"view", "arrangement"},
                   {"trackId", static_cast<int>(trackId)},
                   {"startBeat", startBeat}});
}

juce::var sessionDestination(TrackId trackId, int sceneIndex) {
    return object(
        {{"view", "session"}, {"trackId", static_cast<int>(trackId)}, {"sceneIndex", sceneIndex}});
}

class RemoteServiceLiveTest final : public juce::UnitTest {
  public:
    RemoteServiceLiveTest() : juce::UnitTest("Remote Service Live", "magda") {}

    void runTest() override {
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

            const auto clip = fixture.run("clips.createMidi", object({{"trackId", trackId},
                                                                      {"startBeat", 0.0},
                                                                      {"lengthBeats", 4.0},
                                                                      {"view", "arrangement"}}));
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
            const auto created =
                fixture.run("clips.createMidi", object({{"trackId", static_cast<int>(firstId)},
                                                        {"startBeat", 0.0},
                                                        {"lengthBeats", 4.0},
                                                        {"view", "arrangement"}}));
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

        beginTest("Session clip placement uses explicit empty slots");
        {
            Fixture fixture;
            const auto first =
                fixture.run("tracks.create", object({{"name", "First"}, {"type", "audio"}}));
            const auto second =
                fixture.run("tracks.create", object({{"name", "Second"}, {"type", "audio"}}));
            expect(first.ok && second.ok);
            const auto firstId = static_cast<TrackId>(static_cast<int>(first.result["id"]));
            const auto secondId = static_cast<TrackId>(static_cast<int>(second.result["id"]));
            const auto created =
                fixture.run("clips.createMidi", object({{"trackId", static_cast<int>(firstId)},
                                                        {"startBeat", 0.0},
                                                        {"lengthBeats", 4.0},
                                                        {"view", "session"}}));
            expect(created.ok);
            const auto clipId = static_cast<ClipId>(static_cast<int>(created.result["id"]));
            ClipManager::getInstance().setClipSceneIndex(clipId, 0);

            const auto moved = fixture.run(
                "clips.move", object({{"clipId", static_cast<int>(clipId)},
                                      {"destination", sessionDestination(secondId, 1)}}));
            expect(moved.ok);
            expect(static_cast<int>(moved.result["trackId"]) == secondId);
            expect(static_cast<int>(moved.result["sceneIndex"]) == 1);
            expect(ClipManager::getInstance().getClipInSlot(secondId, 1) == clipId);

            const auto duplicated = fixture.run(
                "clips.duplicate", object({{"clipId", static_cast<int>(clipId)},
                                           {"destination", sessionDestination(firstId, 2)}}));
            expect(duplicated.ok);
            const auto duplicateId = static_cast<ClipId>(static_cast<int>(duplicated.result["id"]));
            expect(ClipManager::getInstance().getClipInSlot(firstId, 2) == duplicateId);

            const auto beforeConflict = fixture.service.currentRevision();
            const auto occupied = fixture.run(
                "clips.move", object({{"clipId", static_cast<int>(clipId)},
                                      {"destination", sessionDestination(firstId, 2)}}));
            expect(!occupied.ok);
            expectEquals(toString(occupied.error.code), juce::String("conflict"));
            expect(fixture.service.currentRevision() == beforeConflict);

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
            const auto clip = fixture.run("clips.createMidi", object({{"trackId", trackId},
                                                                      {"startBeat", 0.0},
                                                                      {"lengthBeats", 4.0},
                                                                      {"view", "arrangement"}}));
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
