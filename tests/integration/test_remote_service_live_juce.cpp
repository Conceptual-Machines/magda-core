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

class RemoteServiceLiveTest final : public juce::UnitTest {
  public:
    RemoteServiceLiveTest() : juce::UnitTest("Remote Service Live", "magda") {}

    void runTest() override {
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
