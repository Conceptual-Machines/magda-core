#pragma once

// Header-only test double for magda::MagdaApi. Provides minimal
// implementations of every sub-interface — Selection, Track, Clip,
// Session, Project are functional; Automation, Alias, and Undo are
// inert stubs that abort on use (the binding tests don't touch them).
//
// The mock records writes (set_volume, set_muted, etc.) into vectors so
// tests can assert what the bindings invoked. Reads are seeded by
// populating the public state members directly.

#include <juce_core/juce_core.h>

#include <cassert>
#include <cstdlib>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "magda/daw/api/alias_api.hpp"
#include "magda/daw/api/automation_api.hpp"
#include "magda/daw/api/clip_api.hpp"
#include "magda/daw/api/focused_api.hpp"
#include "magda/daw/api/magda_api.hpp"
#include "magda/daw/api/midi_api.hpp"
#include "magda/daw/api/project_api.hpp"
#include "magda/daw/api/selection_api.hpp"
#include "magda/daw/api/session_api.hpp"
#include "magda/daw/api/track_api.hpp"
#include "magda/daw/api/transport_api.hpp"
#include "magda/daw/api/undo_api.hpp"
#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipTypes.hpp"
#include "magda/daw/core/TrackInfo.hpp"
#include "magda/daw/core/TrackTypes.hpp"
#include "magda/daw/core/TypeIds.hpp"
// UndoableCommand's full definition lives in UndoManager.hpp; undo_api.hpp
// only forward-declares it. We need the complete type because StubUndoApi
// takes std::unique_ptr<UndoableCommand> by value — MSVC instantiates
// ~unique_ptr eagerly when parsing the inline body, and the default
// deleter static_asserts on a complete type.
#include "magda/daw/core/UndoManager.hpp"
#include "magda/daw/project/ProjectInfo.hpp"

namespace magda::test {

// ---- inert stubs for sub-APIs the bindings don't exercise -------------

class StubAutomationApi : public AutomationApi {
  public:
    AutomationLaneId createLane(const AutomationTarget&, AutomationLaneType) override {
        std::abort();
    }
    AutomationLaneId getLaneForTarget(const AutomationTarget&) const override {
        std::abort();
    }
    AutomationLaneInfo* getLane(AutomationLaneId) override {
        std::abort();
    }
    const AutomationLaneInfo* getLane(AutomationLaneId) const override {
        std::abort();
    }
    AutomationPointId addPoint(AutomationLaneId, double, double, AutomationCurveType) override {
        std::abort();
    }
    void clearLanePoints(AutomationLaneId) override {
        std::abort();
    }
    void beginNotificationBatch() override {
        std::abort();
    }
    void endNotificationBatch() override {
        std::abort();
    }
};

class StubAliasApi : public AliasApi {
  public:
    AliasRegistry& aliasRegistry() override {
        std::abort();
    }
    ResolverRegistry& resolverRegistry() override {
        std::abort();
    }
};

class StubUndoApi : public UndoApi {
  public:
    void executeCommand(std::unique_ptr<UndoableCommand>) override {
        std::abort();
    }
};

// ---- functional sub-APIs ---------------------------------------------

class MockSelectionApi : public SelectionApi {
  public:
    // State (writeable by tests to seed reads)
    TrackId selectedTrack = INVALID_TRACK_ID;
    ClipId selectedClip = INVALID_CLIP_ID;
    std::unordered_set<ClipId> selectedClips;
    bool noteSelectionPresent = false;
    ClipId noteSelectionClipId = INVALID_CLIP_ID;
    std::vector<size_t> noteSelectionIndices;

    // Captured writes
    std::vector<TrackId> trackSelections;
    std::vector<std::unordered_set<TrackId>> tracksSelections;
    std::vector<ClipId> clipSelections;
    std::vector<std::unordered_set<ClipId>> clipsSelections;
    int clearNoteCalls = 0;

    TrackId getSelectedTrack() const override {
        return selectedTrack;
    }
    ClipId getSelectedClip() const override {
        return selectedClip;
    }
    const std::unordered_set<ClipId>& getSelectedClips() const override {
        return selectedClips;
    }
    AutomationLaneId getSelectedAutomationLaneId() const override {
        return INVALID_AUTOMATION_LANE_ID;
    }
    bool hasNoteSelection() const override {
        return noteSelectionPresent;
    }
    ClipId getNoteSelectionClipId() const override {
        return noteSelectionClipId;
    }
    const std::vector<size_t>& getNoteSelectionIndices() const override {
        return noteSelectionIndices;
    }
    void selectTrack(TrackId id) override {
        trackSelections.push_back(id);
    }
    void selectTracks(const std::unordered_set<TrackId>& ids) override {
        tracksSelections.push_back(ids);
    }
    void selectClip(ClipId id) override {
        clipSelections.push_back(id);
    }
    void selectClips(const std::unordered_set<ClipId>& ids) override {
        clipsSelections.push_back(ids);
    }
    void selectNotes(ClipId, const std::vector<size_t>&) override {
        // not exercised by current bindings
    }
    void clearNoteSelection() override {
        ++clearNoteCalls;
    }
};

class MockTrackApi : public TrackApi {
  public:
    std::vector<TrackInfo> tracks;

    struct VolumeWrite {
        TrackId id;
        float value;
    };
    struct PanWrite {
        TrackId id;
        float value;
    };
    struct MuteWrite {
        TrackId id;
        bool value;
    };
    struct SoloWrite {
        TrackId id;
        bool value;
    };
    struct NameWrite {
        TrackId id;
        juce::String value;
    };

    std::vector<TrackInfo> created;
    std::vector<TrackId> deleted;
    std::vector<NameWrite> nameWrites;
    std::vector<VolumeWrite> volumeWrites;
    std::vector<PanWrite> panWrites;
    std::vector<MuteWrite> muteWrites;
    std::vector<SoloWrite> soloWrites;

    TrackId nextId = 1;

    TrackId createTrack(const juce::String& name, TrackType type) override {
        TrackInfo t;
        t.id = nextId++;
        t.name = name;
        t.type = type;
        tracks.push_back(t);
        created.push_back(t);
        return t.id;
    }
    void deleteTrack(TrackId id) override {
        deleted.push_back(id);
    }
    int getNumTracks() const override {
        return static_cast<int>(tracks.size());
    }
    const std::vector<TrackInfo>& getTracks() const override {
        return tracks;
    }
    TrackInfo* getTrack(TrackId id) override {
        for (auto& t : tracks)
            if (t.id == id)
                return &t;
        return nullptr;
    }
    const TrackInfo* getTrack(TrackId id) const override {
        for (auto& t : tracks)
            if (t.id == id)
                return &t;
        return nullptr;
    }
    void setTrackName(TrackId id, const juce::String& name) override {
        nameWrites.push_back({id, name});
    }
    void setTrackVolume(TrackId id, float v, bool /*fromAuto*/) override {
        volumeWrites.push_back({id, v});
    }
    void setTrackPan(TrackId id, float v, bool /*fromAuto*/) override {
        panWrites.push_back({id, v});
    }
    void setTrackMuted(TrackId id, bool v) override {
        muteWrites.push_back({id, v});
    }
    void setTrackSoloed(TrackId id, bool v) override {
        soloWrites.push_back({id, v});
    }
    DeviceId addDeviceToTrack(TrackId, const DeviceInfo&) override {
        return INVALID_DEVICE_ID;
    }
    AudioEngine* getAudioEngine() const override {
        return nullptr;
    }
};

class MockClipApi : public ClipApi {
  public:
    std::unordered_map<ClipId, ClipInfo> clips;
    std::vector<ClipInfo> arrangement;
    std::unordered_map<TrackId, std::vector<ClipId>> clipsOnTrack;

    struct CreateMidi {
        TrackId trackId;
        double startBeats;
        double lengthBeats;
        ClipView view;
    };
    std::vector<CreateMidi> midiCreations;
    std::vector<ClipId> deleted;
    std::vector<std::pair<ClipId, juce::String>> nameWrites;
    std::vector<std::pair<ClipId, juce::String>> grooveWrites;

    ClipId nextId = 100;

    ClipInfo* getClip(ClipId id) override {
        auto it = clips.find(id);
        return it != clips.end() ? &it->second : nullptr;
    }
    std::vector<ClipInfo> getArrangementClips() const override {
        return arrangement;
    }
    std::vector<ClipId> getClipsOnTrack(TrackId trackId) const override {
        auto it = clipsOnTrack.find(trackId);
        return it != clipsOnTrack.end() ? it->second : std::vector<ClipId>{};
    }
    ClipId createMidiClipBeats(TrackId trackId, double start, double length,
                               ClipView view) override {
        midiCreations.push_back({trackId, start, length, view});
        return nextId++;
    }
    void deleteClip(ClipId id) override {
        deleted.push_back(id);
    }
    void setClipName(ClipId id, const juce::String& name) override {
        nameWrites.push_back({id, name});
    }
    void setGrooveTemplate(ClipId id, const juce::String& tmpl) override {
        grooveWrites.push_back({id, tmpl});
    }
    const juce::Array<double>* getCachedTransients(const juce::String&) const override {
        return nullptr;
    }
};

class MockSessionApi : public SessionApi {
  public:
    std::vector<ClipId> launchedClips;
    std::vector<ClipId> stoppedClips;
    std::vector<TrackId> stoppedTracks;
    std::vector<int> launchedScenes;
    int stopAllCalls = 0;
    std::unordered_map<TrackId, ClipId> activeOnTrack;

    void launchClip(ClipId id) override {
        launchedClips.push_back(id);
    }
    void stopClip(ClipId id) override {
        stoppedClips.push_back(id);
    }
    void stopTrack(TrackId id) override {
        stoppedTracks.push_back(id);
    }
    void stopAll() override {
        ++stopAllCalls;
    }
    void launchScene(int sceneIndex) override {
        launchedScenes.push_back(sceneIndex);
    }
    ClipId getActiveClipOnTrack(TrackId id) const override {
        auto it = activeOnTrack.find(id);
        return it != activeOnTrack.end() ? it->second : INVALID_CLIP_ID;
    }

    // Tests can populate slots[(trackId, sceneIndex)] = clipId.
    std::map<std::pair<TrackId, int>, ClipId> slots;
    ClipId getClipInSlot(TrackId trackId, int sceneIndex) const override {
        auto it = slots.find({trackId, sceneIndex});
        return it != slots.end() ? it->second : INVALID_CLIP_ID;
    }

    // Tests can populate clipStates[clipId].
    std::unordered_map<ClipId, SessionClipPlayState> clipStates;
    SessionClipPlayState getClipPlayState(ClipId clipId) const override {
        auto it = clipStates.find(clipId);
        return it != clipStates.end() ? it->second : SessionClipPlayState::Stopped;
    }
};

class MockProjectApi : public ProjectApi {
  public:
    ProjectInfo info;
    const ProjectInfo& getCurrentProjectInfo() const override {
        return info;
    }
};

class MockFocusedApi : public FocusedApi {
  public:
    bool focused = false;
    juce::String focusedName;
    std::vector<juce::String> macroNames;  // index → name
    std::vector<float> macroValues;        // index → value

    struct MacroWrite {
        int idx;
        float value;
    };
    std::vector<MacroWrite> macroWrites;

    bool hasFocus() const override {
        return focused;
    }
    juce::String getFocusedName() const override {
        return focusedName;
    }
    juce::String getMacroName(int idx) const override {
        if (idx < 0 || idx >= static_cast<int>(macroNames.size()))
            return {};
        return macroNames[static_cast<size_t>(idx)];
    }
    float getMacroValue(int idx) const override {
        if (idx < 0 || idx >= static_cast<int>(macroValues.size()))
            return 0.0f;
        return macroValues[static_cast<size_t>(idx)];
    }
    void setMacroValue(int idx, float value) override {
        macroWrites.push_back({idx, value});
    }

    int engageAutoMapCalls = 0;
    int clearAutoMapCalls = 0;
    void engageAutoMap() override {
        ++engageAutoMapCalls;
    }
    void clearAutoMap() override {
        ++clearAutoMapCalls;
    }

    std::vector<int> cycleDeviceCalls;
    void cycleDevice(int direction) override {
        cycleDeviceCalls.push_back(direction);
    }
};

class MockTransportApi : public TransportApi {
  public:
    bool playing = false;
    bool recording = false;
    bool loopEnabled = false;
    double positionBeats = 0.0;

    int playCalls = 0;
    int stopCalls = 0;

    void play() override {
        ++playCalls;
        playing = true;
    }
    void stop() override {
        ++stopCalls;
        playing = false;
        recording = false;
    }
    void setRecording(bool r) override {
        recording = r;
    }
    bool isPlaying() const override {
        return playing;
    }
    bool isRecording() const override {
        return recording;
    }
    bool isLoopEnabled() const override {
        return loopEnabled;
    }
    void setLoopEnabled(bool e) override {
        loopEnabled = e;
    }
    double getPositionBeats() const override {
        return positionBeats;
    }
    void setPositionBeats(double b) override {
        positionBeats = b;
    }
};

class MockMidiApi : public MidiApi {
  public:
    struct Send {
        juce::String port;
        juce::MidiMessage msg;
    };
    std::vector<Send> sends;
    std::vector<juce::String> outputPortNames;
    juce::String defaultOutputPort;

    bool sendMidi(const juce::String& port, const juce::MidiMessage& msg) override {
        sends.push_back({port, msg});
        return true;
    }
    bool sendSysEx(const juce::String& port, const juce::uint8* data, size_t numBytes) override {
        sends.push_back(
            {port, juce::MidiMessage::createSysExMessage(data, static_cast<int>(numBytes))});
        return true;
    }
    std::vector<juce::String> getOutputPortNames() const override {
        return outputPortNames;
    }
    juce::String getDefaultOutputPort() const override {
        return defaultOutputPort;
    }
};

// ---- composite -------------------------------------------------------

class MockMagdaApi : public MagdaApi {
  public:
    MockSelectionApi selection_;
    StubAutomationApi automation_;
    StubAliasApi aliases_;
    MockTrackApi tracks_;
    MockClipApi clips_;
    MockSessionApi session_;
    MockProjectApi project_;
    StubUndoApi undo_;
    MockMidiApi midi_;
    MockTransportApi transport_;
    MockFocusedApi focused_;

    SelectionApi& selection() override {
        return selection_;
    }
    AutomationApi& automation() override {
        return automation_;
    }
    AliasApi& aliases() override {
        return aliases_;
    }
    TrackApi& tracks() override {
        return tracks_;
    }
    ClipApi& clips() override {
        return clips_;
    }
    SessionApi& session() override {
        return session_;
    }
    ProjectApi& project() override {
        return project_;
    }
    UndoApi& undo() override {
        return undo_;
    }
    MidiApi& midi() override {
        return midi_;
    }
    TransportApi& transport() override {
        return transport_;
    }
    FocusedApi& focused() override {
        return focused_;
    }
};

}  // namespace magda::test
