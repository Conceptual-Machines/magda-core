#pragma once

// Header-only test double for magda::MagdaApi. Provides minimal
// implementations of every sub-interface — Selection, Track, Clip,
// Session, Project, and Undo are functional enough for tests; Automation
// and Alias are inert stubs that abort on use (the binding tests don't touch them).
//
// The mock records writes (set_volume, set_muted, etc.) into vectors so
// tests can assert what the bindings invoked. Reads are seeded by
// populating the public state members directly.

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "magda/daw/api/alias_api.hpp"
#include "magda/daw/api/automation_api.hpp"
#include "magda/daw/api/clip_api.hpp"
#include "magda/daw/api/device_api.hpp"
#include "magda/daw/api/focused_api.hpp"
#include "magda/daw/api/groove_api.hpp"
#include "magda/daw/api/magda_api.hpp"
#include "magda/daw/api/midi_api.hpp"
#include "magda/daw/api/plugin_api.hpp"
#include "magda/daw/api/project_api.hpp"
#include "magda/daw/api/selection_api.hpp"
#include "magda/daw/api/session_api.hpp"
#include "magda/daw/api/track_api.hpp"
#include "magda/daw/api/transport_api.hpp"
#include "magda/daw/api/undo_api.hpp"
#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipTypes.hpp"
#include "magda/daw/core/ParameterUtils.hpp"
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
    /// Seedable, unlike the rest of this stub. `automation.listLanes` backs the
    /// `automation` subscription topic, so enumerating lanes has to be a read a
    /// test can drive rather than an abort.
    std::vector<AutomationLaneInfo> lanes;

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
    const std::vector<AutomationLaneInfo>& getLanes() const override {
        return lanes;
    }
    std::vector<AutomationLaneId> getLanesForTrack(TrackId) const override {
        std::abort();
    }
    std::vector<AutomationLaneId> getEditScopedLanes() const override {
        std::abort();
    }
    const std::vector<AutomationClipInfo>& getClips() const override {
        std::abort();
    }
    bool setLanePoints(AutomationLaneId, std::vector<AutomationPoint>) override {
        std::abort();
    }
    bool deleteLane(AutomationLaneId) override {
        std::abort();
    }
    bool retypeEmptyLane(AutomationLaneId, AutomationLaneType) override {
        std::abort();
    }
    AutomationClipId createClip(AutomationLaneId, double, double) override {
        std::abort();
    }
    AutomationClipInfo* getClip(AutomationClipId) override {
        std::abort();
    }
    const AutomationClipInfo* getClip(AutomationClipId) const override {
        std::abort();
    }
    void deleteClip(AutomationClipId) override {
        std::abort();
    }
    void moveClip(AutomationClipId, double) override {
        std::abort();
    }
    void resizeClip(AutomationClipId, double, bool) override {
        std::abort();
    }
    AutomationClipId duplicateClip(AutomationClipId) override {
        std::abort();
    }
    void setClipName(AutomationClipId, const juce::String&) override {
        std::abort();
    }
    void setClipColour(AutomationClipId, juce::Colour) override {
        std::abort();
    }
    void setClipLooping(AutomationClipId, bool) override {
        std::abort();
    }
    void setClipLoopLength(AutomationClipId, double) override {
        std::abort();
    }
    void setClipPoints(AutomationClipId, std::vector<AutomationPoint>) override {
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
    int executeCalls = 0;
    int compoundDepth = 0;

    /// Descriptions of every compound opened, in order, and the depth reached.
    /// The remote dispatcher promises one named undo step per mutating request,
    /// which is only assertable if the names and the nesting are both visible.
    std::vector<juce::String> compoundDescriptions;
    int maxCompoundDepth = 0;

    /// Retained, not dropped: callers read ids off the raw pointer after this
    /// returns, so destroying the command here would dangle them. Deliberately
    /// not executed — commands act on the real singletons, which a mock-facade
    /// test is not driving.
    std::vector<std::unique_ptr<UndoableCommand>> commands;

    void executeCommand(std::unique_ptr<UndoableCommand> command) override {
        ++executeCalls;
        commands.push_back(std::move(command));
    }
    void beginCompound(const juce::String& description) override {
        ++compoundDepth;
        maxCompoundDepth = std::max(maxCompoundDepth, compoundDepth);
        compoundDescriptions.push_back(description);
    }
    void endCompound() override {
        --compoundDepth;
    }
};

// ---- functional sub-APIs ---------------------------------------------

class MockSelectionApi : public SelectionApi {
  public:
    // State (writeable by tests to seed reads)
    TrackId selectedTrack = INVALID_TRACK_ID;
    ClipId selectedClip = INVALID_CLIP_ID;
    std::unordered_set<ClipId> selectedClips;
    ChainNodePath selectedChainNode;
    AutomationLaneId selectedAutomationLane = INVALID_AUTOMATION_LANE_ID;
    bool noteSelectionPresent = false;
    ClipId noteSelectionClipId = INVALID_CLIP_ID;
    std::vector<size_t> noteSelectionIndices;

    // Captured writes
    std::vector<TrackId> trackSelections;
    std::vector<std::unordered_set<TrackId>> tracksSelections;
    std::vector<ClipId> clipSelections;
    std::vector<std::unordered_set<ClipId>> clipsSelections;
    int clearNoteCalls = 0;
    int clearSelectionCalls = 0;

    TrackId getSelectedTrack() const override {
        return selectedTrack;
    }
    ClipId getSelectedClip() const override {
        return selectedClip;
    }
    const std::unordered_set<ClipId>& getSelectedClips() const override {
        return selectedClips;
    }
    ChainNodePath getSelectedChainNode() const override {
        return selectedChainNode;
    }
    AutomationLaneId getSelectedAutomationLaneId() const override {
        return selectedAutomationLane;
    }
    AutomationClipId getSelectedAutomationClipId() const override {
        return INVALID_AUTOMATION_CLIP_ID;
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
    void selectAutomationClip(AutomationClipId, AutomationLaneId) override {
        // not exercised by current DSL binding tests
    }
    void selectNotes(ClipId, const std::vector<size_t>&) override {
        // not exercised by current bindings
    }
    void clearNoteSelection() override {
        ++clearNoteCalls;
    }
    std::vector<AutomationLaneId> automationLaneSelections;

    void selectAutomationLane(AutomationLaneId laneId) override {
        automationLaneSelections.push_back(laneId);
        selectedAutomationLane = laneId;
    }
    void clearSelection() override {
        ++clearSelectionCalls;
        selectedTrack = INVALID_TRACK_ID;
        selectedClip = INVALID_CLIP_ID;
        selectedClips.clear();
        noteSelectionPresent = false;
        noteSelectionClipId = INVALID_CLIP_ID;
        noteSelectionIndices.clear();
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
    struct RecordArmWrite {
        TrackId id;
        bool value;
    };
    struct NameWrite {
        TrackId id;
        juce::String value;
    };
    struct ColourWrite {
        TrackId id;
        juce::Colour value;
    };
    struct GroupWrite {
        std::vector<TrackId> ids;
        juce::String name;
        TrackId groupId;
    };
    struct MoveWrite {
        TrackId id;
        int position;
    };
    struct DeviceWrite {
        TrackId trackId;
        DeviceInfo device;
    };

    std::vector<TrackInfo> created;
    std::vector<TrackId> deleted;
    std::vector<NameWrite> nameWrites;
    std::vector<ColourWrite> colourWrites;
    std::vector<GroupWrite> groupWrites;
    std::vector<MoveWrite> moveWrites;
    std::vector<VolumeWrite> volumeWrites;
    std::vector<PanWrite> panWrites;
    std::vector<MuteWrite> muteWrites;
    std::vector<SoloWrite> soloWrites;
    std::vector<RecordArmWrite> recordArmWrites;
    std::vector<DeviceWrite> deviceWrites;

    TrackId nextId = 1;
    DeviceId nextDeviceId = 1;

    TrackId createTrack(const juce::String& name, TrackType type) override {
        TrackInfo t;
        t.id = nextId++;
        t.name = name;
        t.type = type;
        tracks.push_back(t);
        created.push_back(t);
        return t.id;
    }
    TrackId groupTracks(const std::vector<TrackId>& ids, const juce::String& name) override {
        TrackInfo group;
        group.id = nextId++;
        group.name = name;
        group.type = TrackType::Group;
        group.childIds = ids;
        tracks.push_back(group);
        groupWrites.push_back({ids, name, group.id});
        return group.id;
    }
    void deleteTrack(TrackId id) override {
        deleted.push_back(id);
    }
    void moveTrackToPosition(TrackId id, int oneBasedPosition) override {
        moveWrites.push_back({id, oneBasedPosition});
    }
    int getNumTracks() const override {
        return static_cast<int>(tracks.size());
    }
    /// Counted so a test can assert how often the model was read, not only what
    /// came back — the subscription hub promises one projection per flush no
    /// matter how many clients are watching.
    mutable int getTracksCalls = 0;

    const std::vector<TrackInfo>& getTracks() const override {
        ++getTracksCalls;
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
    void setTrackColour(TrackId id, juce::Colour colour) override {
        colourWrites.push_back({id, colour});
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
    void setTrackRecordArmed(TrackId id, bool v) override {
        recordArmWrites.push_back({id, v});
    }
    DeviceId addDeviceToTrack(TrackId trackId, const DeviceInfo& device) override {
        deviceWrites.push_back({trackId, device});
        return nextDeviceId++;
    }
    // -----------------------------------------------------------------------
    // Racks and chains
    //
    // A real nested tree, held in `TrackInfo::chain.fxChainElements` exactly
    // as `TrackManager` holds it, resolved by walking `ChainNodePath` the same
    // way `getRackByPath` does.
    //
    // Every one of these used to return `INVALID_*_ID` or nullptr. That made
    // the mock agree with a facade that could not address nested racks (#1993)
    // — it had nothing to disagree with — so the gap was invisible from the
    // tests. Modelling it properly is what stops it coming back: a facade that
    // silently loses depth now fails here rather than in a bug report.
    // -----------------------------------------------------------------------

    RackId nextRackId = 1;
    ChainId nextChainId = 1;

    RackInfo* resolveRack(const ChainNodePath& rackPath) {
        auto* track = getTrack(rackPath.trackId);
        if (track == nullptr)
            return nullptr;
        if (rackPath.isTrackLevel || rackPath.topLevelDeviceId != INVALID_DEVICE_ID)
            return nullptr;

        // Mirrors `TrackManager::getRackByPath` step for step, including its
        // structural rules: a route alternates `Rack > Chain > Rack > Chain`,
        // a Device is a leaf, and a Segment only ever leads. Two consecutive
        // steps of the same kind move sideways through the tree on ids that all
        // exist, which is a route the model cannot express — see the long note
        // on the model's copy for the shapes that used to resolve.
        RackInfo* rack = nullptr;
        ChainInfo* chain = nullptr;
        for (std::size_t index = 0; index < rackPath.steps.size(); ++index) {
            const auto& step = rackPath.steps[index];
            const bool isLast = index + 1 == rackPath.steps.size();

            if (step.type == ChainStepType::Rack) {
                if (rack != nullptr && chain == nullptr)
                    return nullptr;
                // A rack sits either directly in the track's FX list or inside
                // the chain reached by the previous step.
                auto& elements = chain != nullptr ? chain->elements : track->chain.fxChainElements;
                RackInfo* found = nullptr;
                for (auto& element : elements) {
                    if (magda::isRack(element) && magda::getRack(element).id == step.id) {
                        found = &magda::getRack(element);
                        break;
                    }
                }
                if (found == nullptr)
                    return nullptr;
                rack = found;
                chain = nullptr;
            } else if (step.type == ChainStepType::Chain) {
                if (rack == nullptr || chain != nullptr)
                    return nullptr;
                ChainInfo* found = nullptr;
                for (auto& candidate : rack->chains) {
                    if (candidate.id == step.id) {
                        found = &candidate;
                        break;
                    }
                }
                if (found == nullptr)
                    return nullptr;
                chain = found;
            } else if (step.type == ChainStepType::Device) {
                if (!isLast || chain == nullptr)
                    return nullptr;
                const auto found = std::find_if(chain->elements.begin(), chain->elements.end(),
                                                [&step](const ChainElement& element) {
                                                    return magda::isDevice(element) &&
                                                           magda::getDevice(element).id == step.id;
                                                });
                if (found == chain->elements.end())
                    return nullptr;
            } else if (step.type == ChainStepType::Segment) {
                return nullptr;
            }
        }
        // The deepest rack traversed, whatever the last step was — so a *chain*
        // path answers with that chain's parent rack rather than with nothing.
        //
        // That is `TrackManager::getRackByPath`'s behaviour, and matching it is
        // the point of this mock. Guarding on the last step type here instead
        // read better and was wrong: the two resolvers then disagreed, and a
        // test written against the mock encoded the mock's opinion rather than
        // the model's. Whether the real one *should* be this lenient is a
        // separate question from whether the mock should lie about it.
        return rack;
    }

    ChainInfo* resolveChain(const ChainNodePath& chainPath) {
        if (chainPath.steps.empty() || chainPath.steps.back().type != ChainStepType::Chain)
            return nullptr;
        // A chain hangs directly off a rack. Resolving the prefix is not enough
        // by itself — for `rack > chain1 > chain2` the prefix is legal, and the
        // search below would find `chain2` beside `chain1` in the same rack.
        // Mirrors the same guard in `TrackManager::getChainByPath`.
        if (chainPath.steps.size() < 2 ||
            chainPath.steps[chainPath.steps.size() - 2].type != ChainStepType::Rack)
            return nullptr;
        auto* rack = resolveRack(chainPath.parent());
        if (rack == nullptr)
            return nullptr;
        for (auto& chain : rack->chains) {
            if (chain.id == chainPath.steps.back().id)
                return &chain;
        }
        return nullptr;
    }

    RackId addRackToChainByPath(const ChainNodePath& chainPath, const juce::String& name) override {
        auto* chain = resolveChain(chainPath);
        if (chain == nullptr)
            return INVALID_RACK_ID;
        RackInfo rack;
        rack.id = nextRackId++;
        rack.name = name;
        // A new rack comes with one chain, like the real thing — otherwise a
        // test that nests two racks has nowhere to put the second.
        ChainInfo defaultChain;
        defaultChain.id = nextChainId++;
        defaultChain.name = "Chain 1";
        rack.chains.push_back(std::move(defaultChain));
        const auto id = rack.id;
        chain->elements.push_back(makeRackElement(std::move(rack)));
        return id;
    }

    void removeRackFromChainByPath(const ChainNodePath& rackPath) override {
        if (rackPath.steps.empty() || rackPath.steps.back().type != ChainStepType::Rack)
            return;
        const auto rackId = rackPath.steps.back().id;
        const auto parent = rackPath.parent();

        std::vector<ChainElement>* elements = nullptr;
        if (parent.steps.empty()) {
            if (auto* track = getTrack(parent.trackId))
                elements = &track->chain.fxChainElements;
        } else if (auto* chain = resolveChain(parent)) {
            elements = &chain->elements;
        }
        if (elements == nullptr)
            return;

        elements->erase(std::remove_if(elements->begin(), elements->end(),
                                       [rackId](const ChainElement& element) {
                                           return magda::isRack(element) &&
                                                  magda::getRack(element).id == rackId;
                                       }),
                        elements->end());
    }

    const RackInfo* getRackByPath(const ChainNodePath& rackPath) const override {
        return const_cast<MockTrackApi*>(this)->resolveRack(rackPath);
    }

    void setRackBypassedByPath(const ChainNodePath& rackPath, bool bypassed) override {
        if (auto* rack = resolveRack(rackPath))
            rack->bypassed = bypassed;
    }

    void setRackVolume(const ChainNodePath& rackPath, float volumeDb) override {
        if (auto* rack = resolveRack(rackPath))
            rack->volume = volumeDb;
    }

    ChainId addChainToRack(const ChainNodePath& rackPath, const juce::String& name) override {
        auto* rack = resolveRack(rackPath);
        if (rack == nullptr)
            return INVALID_CHAIN_ID;
        ChainInfo chain;
        chain.id = nextChainId++;
        chain.name = name;
        const auto id = chain.id;
        rack->chains.push_back(std::move(chain));
        return id;
    }

    void removeChainByPath(const ChainNodePath& chainPath) override {
        if (chainPath.steps.empty() || chainPath.steps.back().type != ChainStepType::Chain)
            return;
        const auto chainId = chainPath.steps.back().id;
        if (auto* rack = resolveRack(chainPath.parent())) {
            auto& chains = rack->chains;
            chains.erase(std::remove_if(chains.begin(), chains.end(),
                                        [chainId](const ChainInfo& c) { return c.id == chainId; }),
                         chains.end());
        }
    }

    const ChainInfo* getChainByPath(const ChainNodePath& chainPath) const override {
        return const_cast<MockTrackApi*>(this)->resolveChain(chainPath);
    }

    void setChainOutput(const ChainNodePath& chainPath, int outputIndex) override {
        if (auto* chain = resolveChain(chainPath))
            chain->outputIndex = outputIndex;
    }
    void setChainMuted(const ChainNodePath& chainPath, bool muted) override {
        if (auto* chain = resolveChain(chainPath))
            chain->muted = muted;
    }
    void setChainBypassed(const ChainNodePath& chainPath, bool bypassed) override {
        if (auto* chain = resolveChain(chainPath))
            chain->bypassed = bypassed;
    }
    void setChainSolo(const ChainNodePath& chainPath, bool solo) override {
        if (auto* chain = resolveChain(chainPath))
            chain->solo = solo;
    }
    void setChainVolume(const ChainNodePath& chainPath, float volumeDb) override {
        if (auto* chain = resolveChain(chainPath))
            chain->volume = volumeDb;
    }
    void setChainPan(const ChainNodePath& chainPath, float pan) override {
        if (auto* chain = resolveChain(chainPath))
            chain->pan = pan;
    }
    void setChainName(const ChainNodePath& chainPath, const juce::String& name) override {
        if (auto* chain = resolveChain(chainPath))
            chain->name = name;
    }

    DeviceId addDeviceToChainByPath(const ChainNodePath& chainPath,
                                    const DeviceInfo& device) override {
        auto* chain = resolveChain(chainPath);
        if (chain == nullptr)
            return INVALID_DEVICE_ID;
        DeviceInfo copy = device;
        copy.id = nextDeviceId++;
        const auto id = copy.id;
        chain->elements.push_back(ChainElement{std::move(copy)});
        return id;
    }

    RackId addRackToTrack(TrackId trackId, const juce::String& name) override {
        auto* track = getTrack(trackId);
        if (track == nullptr)
            return INVALID_RACK_ID;
        RackInfo rack;
        rack.id = nextRackId++;
        rack.name = name;
        ChainInfo defaultChain;
        defaultChain.id = nextChainId++;
        defaultChain.name = "Chain 1";
        rack.chains.push_back(std::move(defaultChain));
        const auto id = rack.id;
        track->chain.fxChainElements.push_back(makeRackElement(std::move(rack)));
        return id;
    }

    // The triple-based surface, as shims — the same shape `TrackApiLive` uses,
    // so a test driving either form exercises one model.
    DeviceId addDeviceToChain(TrackId trackId, RackId rackId, ChainId chainId,
                              const DeviceInfo& device) override {
        return addDeviceToChainByPath(ChainNodePath::chain(trackId, rackId, chainId), device);
    }
    void removeRackFromTrack(TrackId trackId, RackId rackId) override {
        removeRackFromChainByPath(ChainNodePath::rack(trackId, rackId));
    }
    const RackInfo* getRack(TrackId trackId, RackId rackId) const override {
        return getRackByPath(ChainNodePath::rack(trackId, rackId));
    }
    void setRackBypassed(TrackId trackId, RackId rackId, bool bypassed) override {
        setRackBypassedByPath(ChainNodePath::rack(trackId, rackId), bypassed);
    }
    void setRackVolume(TrackId trackId, RackId rackId, float volumeDb) override {
        setRackVolume(ChainNodePath::rack(trackId, rackId), volumeDb);
    }
    ChainId addChainToRack(TrackId trackId, RackId rackId, const juce::String& name) override {
        return addChainToRack(ChainNodePath::rack(trackId, rackId), name);
    }
    void removeChainFromRack(TrackId trackId, RackId rackId, ChainId chainId) override {
        removeChainByPath(ChainNodePath::chain(trackId, rackId, chainId));
    }
    const ChainInfo* getChain(TrackId trackId, RackId rackId, ChainId chainId) const override {
        return getChainByPath(ChainNodePath::chain(trackId, rackId, chainId));
    }
    void setChainOutput(TrackId trackId, RackId rackId, ChainId chainId, int outputIndex) override {
        setChainOutput(ChainNodePath::chain(trackId, rackId, chainId), outputIndex);
    }
    void setChainMuted(TrackId trackId, RackId rackId, ChainId chainId, bool muted) override {
        setChainMuted(ChainNodePath::chain(trackId, rackId, chainId), muted);
    }
    void setChainBypassed(TrackId trackId, RackId rackId, ChainId chainId, bool bypassed) override {
        setChainBypassed(ChainNodePath::chain(trackId, rackId, chainId), bypassed);
    }
    void setChainSolo(TrackId trackId, RackId rackId, ChainId chainId, bool solo) override {
        setChainSolo(ChainNodePath::chain(trackId, rackId, chainId), solo);
    }
    void setChainVolume(TrackId trackId, RackId rackId, ChainId chainId, float volumeDb) override {
        setChainVolume(ChainNodePath::chain(trackId, rackId, chainId), volumeDb);
    }
    void setChainPan(TrackId trackId, RackId rackId, ChainId chainId, float pan) override {
        setChainPan(ChainNodePath::chain(trackId, rackId, chainId), pan);
    }
    void setChainName(TrackId trackId, RackId rackId, ChainId chainId,
                      const juce::String& name) override {
        setChainName(ChainNodePath::chain(trackId, rackId, chainId), name);
    }

    const DeviceInfo* getPrimaryInstrument(TrackId) const override {
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
    std::vector<std::pair<ClipId, bool>> enabledWrites;
    std::vector<std::pair<ClipId, juce::String>> grooveWrites;
    std::vector<std::pair<ClipId, MidiNote>> noteAdds;

    struct QuantizeCall {
        ClipId clipId;
        std::vector<size_t> noteIndices;
        double gridResolution;
        MidiNoteQuantizeMode mode;
    };
    std::vector<QuantizeCall> quantizeCalls;

    struct SliceCall {
        ClipId clipId;
        std::vector<size_t> noteIndices;
        int subdivisions;
    };
    std::vector<SliceCall> sliceCalls;

    std::vector<std::pair<ClipId, int>> transposeCalls;

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
    void setClipEnabled(ClipId id, bool enabled) override {
        enabledWrites.push_back({id, enabled});
    }
    void setGrooveTemplate(ClipId id, const juce::String& tmpl) override {
        grooveWrites.push_back({id, tmpl});
    }
    bool addMidiNote(ClipId id, double startBeat, int noteNumber, double lengthBeats,
                     int velocity) override {
        auto* clip = getClip(id);
        if (clip == nullptr || !clip->isMidi() || lengthBeats <= 0.0)
            return false;

        MidiNote note;
        note.startBeat = startBeat;
        note.noteNumber = noteNumber;
        note.lengthBeats = lengthBeats;
        note.velocity = velocity;
        noteAdds.push_back({id, note});
        clip->midiNotes.push_back(note);
        return true;
    }
    bool quantizeMidiNotes(ClipId id, const std::vector<size_t>& noteIndices, double gridResolution,
                           MidiNoteQuantizeMode mode) override {
        auto* clip = getClip(id);
        if (clip == nullptr || !clip->isMidi() || noteIndices.empty() || gridResolution <= 0.0)
            return false;

        quantizeCalls.push_back({id, noteIndices, gridResolution, mode});
        for (auto index : noteIndices) {
            if (index >= clip->midiNotes.size())
                continue;

            auto& note = clip->midiNotes[index];
            if (mode == MidiNoteQuantizeMode::StartOnly ||
                mode == MidiNoteQuantizeMode::StartAndLength)
                note.startBeat = std::round(note.startBeat / gridResolution) * gridResolution;
            if (mode == MidiNoteQuantizeMode::LengthOnly ||
                mode == MidiNoteQuantizeMode::StartAndLength)
                note.lengthBeats = std::max(
                    gridResolution, std::round(note.lengthBeats / gridResolution) * gridResolution);
        }
        return true;
    }
    bool sliceMidiNotes(ClipId id, const std::vector<size_t>& noteIndices,
                        int subdivisions) override {
        auto* clip = getClip(id);
        if (clip == nullptr || !clip->isMidi() || noteIndices.empty() || subdivisions < 2)
            return false;

        sliceCalls.push_back({id, noteIndices, subdivisions});
        auto original = clip->midiNotes;
        std::vector<MidiNote> sliced;
        for (size_t index = 0; index < original.size(); ++index) {
            const bool shouldSlice =
                std::find(noteIndices.begin(), noteIndices.end(), index) != noteIndices.end();
            if (!shouldSlice || original[index].lengthBeats <= 0.0) {
                sliced.push_back(original[index]);
                continue;
            }

            const double sliceLength = original[index].lengthBeats / subdivisions;
            for (int slice = 0; slice < subdivisions; ++slice) {
                auto note = original[index];
                note.startBeat += sliceLength * slice;
                note.lengthBeats = sliceLength;
                sliced.push_back(note);
            }
        }
        clip->midiNotes = std::move(sliced);
        return true;
    }
    bool transposeMidiClip(ClipId id, int semitones) override {
        auto* clip = getClip(id);
        if (clip == nullptr || !clip->isMidi())
            return false;

        transposeCalls.push_back({id, semitones});
        for (auto& note : clip->midiNotes)
            note.noteNumber += semitones;
        return true;
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
    void setTempo(double bpm) override {
        info.tempo = bpm;
    }
    void setTimeSignature(int numerator, int denominator) override {
        info.timeSignatureNumerator = numerator;
        info.timeSignatureDenominator = denominator;
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
    int refreshStateSourceCalls = 0;

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

    /// A fixed meter, so a test that seeks by bars gets an arithmetic answer.
    /// Set it to something else to check that seeking follows the meter rather
    /// than assuming four.
    double beatsPerBar = 4.0;

    /// The last offset seekBars passed down, so a test can see what the
    /// facade's clamp did to an out-of-range one.
    mutable int lastBarOffset = 0;

    double beatsAtBarOffset(double beats, int deltaBars) const override {
        lastBarOffset = deltaBars;
        return beats + (beatsPerBar * deltaBars);
    }

    int addStateListener(StateListener listener) override {
        const auto token = nextStateListenerToken++;
        stateListeners.emplace_back(token, std::move(listener));
        return token;
    }

    void removeStateListener(int token) override {
        stateListeners.erase(
            std::remove_if(stateListeners.begin(), stateListeners.end(),
                           [token](const auto& entry) { return entry.first == token; }),
            stateListeners.end());
    }

    void refreshStateSource() override {
        ++refreshStateSourceCalls;
    }

    void notifyStateChanged() {
        const auto listeners = stateListeners;
        for (const auto& [_, listener] : listeners)
            listener();
    }

    int stateListenerCount() const {
        return static_cast<int>(stateListeners.size());
    }

  private:
    int nextStateListenerToken = 1;
    std::vector<std::pair<int, StateListener>> stateListeners;
};

class MockMidiApi : public MidiApi {
  public:
    struct Send {
        juce::String port;
        juce::MidiMessage msg;
    };
    std::vector<Send> sends;
    struct Injection {
        TrackId trackId;
        juce::MidiMessage msg;
    };
    std::vector<Injection> injections;
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
    bool injectTrackMidi(TrackId trackId, const juce::MidiMessage& msg) override {
        injections.push_back({trackId, msg});
        return true;
    }
};

class MockPluginApi : public PluginApi {
  public:
    std::vector<DeviceInfo> preferred;
    std::vector<DeviceInfo> all;

    std::vector<DeviceInfo> getExternalPlugins() const override {
        return preferred;
    }
    std::vector<DeviceInfo> getAllExternalPlugins() const override {
        return all.empty() ? preferred : all;
    }
    std::optional<SequencerRuntimeContext> getStepSequencerContext(
        const ChainNodePath&) const override {
        return std::nullopt;
    }
    std::optional<SequencerRuntimeContext> getPolySequencerContext(
        const ChainNodePath&) const override {
        return std::nullopt;
    }
    juce::String applyStepSequencerPattern(const ChainNodePath&,
                                           const StepSequencerPattern&) override {
        return {};
    }
    juce::String applyPolySequencerPattern(const ChainNodePath&,
                                           const PolySequencerPattern&) override {
        return {};
    }
    juce::String applyFourOscUpdate(const ChainNodePath&, const FourOscUpdate&) override {
        return {};
    }
    juce::String applyFaustSource(const ChainNodePath&, const juce::String&, const juce::String&,
                                  bool) override {
        return {};
    }
};

class MockDeviceApi : public DeviceApi {
  public:
    std::vector<DeviceCatalogEntry> catalog;
    // Live devices, keyed by the path that addresses them.
    std::map<ChainNodePath, DeviceInfo> devices;

    std::vector<DeviceCatalogEntry> getCatalog() const override {
        return catalog;
    }
    std::optional<DeviceCatalogEntry> findCatalogEntry(
        const juce::String& catalogId) const override {
        for (const auto& entry : catalog) {
            if (entry.catalogId == catalogId)
                return entry;
        }
        return std::nullopt;
    }
    const DeviceInfo* getDevice(const ChainNodePath& devicePath) const override {
        const auto it = devices.find(devicePath);
        return it != devices.end() ? &it->second : nullptr;
    }
    std::vector<DeviceParameter> getDeviceParameters(
        const ChainNodePath& devicePath) const override {
        const auto* device = getDevice(devicePath);
        if (device == nullptr)
            return {};
        std::vector<DeviceParameter> parameters;
        for (const auto& info : device->parameters) {
            parameters.push_back({info.paramIndex, info.stableId, info.name, info.unit,
                                  info.minValue, info.maxValue,
                                  ParameterUtils::modelToRealValue({info.defaultValue}, info),
                                  ParameterUtils::modelToRealValue({info.currentValue}, info)});
        }
        return parameters;
    }

    // Recorded mutations, for asserting what a caller invoked.
    struct AddRecord {
        ChainNodePath parentPath;
        juce::String catalogId;
        int index = -1;
    };
    std::vector<AddRecord> added;
    std::vector<ChainNodePath> removed;
    std::vector<std::pair<ChainNodePath, int>> moved;
    std::vector<std::pair<ChainNodePath, bool>> bypassed;
    std::vector<std::tuple<ChainNodePath, int, float>> parameterWrites;
    DeviceId nextDeviceId = 1;

    DeviceId addDevice(const ChainNodePath& parentPath, const juce::String& catalogId,
                       int index) override {
        if (!findCatalogEntry(catalogId).has_value())
            return INVALID_DEVICE_ID;
        added.push_back({parentPath, catalogId, index});
        return nextDeviceId++;
    }
    bool removeDevice(const ChainNodePath& devicePath) override {
        if (getDevice(devicePath) == nullptr)
            return false;
        removed.push_back(devicePath);
        return true;
    }
    bool moveDevice(const ChainNodePath& devicePath, int toIndex) override {
        if (toIndex < 0 || getDevice(devicePath) == nullptr)
            return false;
        moved.emplace_back(devicePath, toIndex);
        return true;
    }
    bool setDeviceBypassed(const ChainNodePath& devicePath, bool value) override {
        if (getDevice(devicePath) == nullptr)
            return false;
        bypassed.emplace_back(devicePath, value);
        return true;
    }
    bool setDeviceParameter(const ChainNodePath& devicePath, int paramIndex, float value) override {
        const auto it = devices.find(devicePath);
        if (it == devices.end())
            return false;
        for (auto& info : it->second.parameters) {
            if (info.paramIndex != paramIndex)
                continue;
            if (value < info.minValue || value > info.maxValue)
                return false;
            // The live manager updates the model synchronously — and stores the
            // model-domain value, which for a configured external parameter is
            // TE-native, not the display value the caller sent.
            const auto model = ParameterUtils::realToModelValue(value, info);
            info.currentValue = model.value;
            parameterWrites.emplace_back(devicePath, paramIndex, model.value);
            return true;
        }
        return false;
    }

    // Mirrors the live semantics: each provided list replaces that selection
    // on the stored device, exactly what refreshLiveDevices would produce.
    std::vector<std::pair<ChainNodePath, DeviceParameterConfigUpdate>> configUpdates;

    bool setDeviceParameterConfig(const ChainNodePath& devicePath,
                                  const DeviceParameterConfigUpdate& update) override {
        const auto it = devices.find(devicePath);
        if (it == devices.end())
            return false;
        auto& device = it->second;
        if (device.format == PluginFormat::Internal)
            return false;
        const auto count = static_cast<int>(device.parameters.size());
        for (const auto* indices :
             {&update.visibleParameters, &update.miniMixerParameters, &update.aiAgentParameters}) {
            if (!indices->has_value())
                continue;
            for (const int index : **indices) {
                if (index < 0 || index >= count)
                    return false;
            }
        }
        if (update.parameterOverrides) {
            for (const auto& override_ : *update.parameterOverrides) {
                if (override_.index < 0 || override_.index >= count)
                    return false;
                const auto& info = device.parameters[static_cast<size_t>(override_.index)];
                if (override_.minValue.value_or(info.minValue) >=
                    override_.maxValue.value_or(info.maxValue))
                    return false;
            }
        }
        if (update.visibleParameters)
            device.visibleParameters = *update.visibleParameters;
        if (update.miniMixerParameters)
            device.miniMixerParameters = *update.miniMixerParameters;
        if (update.aiAgentParameters)
            device.aiSoundDesignerParameters = *update.aiAgentParameters;
        if (update.aiPrompt)
            device.aiSoundDesignerPrompt = *update.aiPrompt;
        if (update.parameterOverrides) {
            for (const auto& override_ : *update.parameterOverrides) {
                auto& info = device.parameters[static_cast<size_t>(override_.index)];
                if (override_.unit)
                    info.unit = *override_.unit;
                if (override_.scale)
                    info.scale = *override_.scale;
                if (override_.minValue)
                    info.minValue = *override_.minValue;
                if (override_.maxValue)
                    info.maxValue = *override_.maxValue;
                if (override_.choices)
                    info.choices = *override_.choices;
            }
        }
        configUpdates.emplace_back(devicePath, update);
        return true;
    }

    std::vector<ChainNodePath> openedEditors;

    bool openDeviceEditor(const ChainNodePath& devicePath) override {
        if (getDevice(devicePath) == nullptr)
            return false;
        openedEditors.push_back(devicePath);
        return true;
    }
};

class MockGrooveApi : public GrooveApi {
  public:
    juce::StringArray names;

    bool upsertTemplate(const juce::String& name, int, bool, const std::vector<float>&) override {
        names.addIfNotAlreadyThere(name);
        return true;
    }
    juce::StringArray getTemplateNames() const override {
        return names;
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
    MockPluginApi plugins_;
    MockDeviceApi devices_;
    MockGrooveApi grooves_;

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
    PluginApi& plugins() override {
        return plugins_;
    }
    DeviceApi& devices() override {
        return devices_;
    }
    GrooveApi& grooves() override {
        return grooves_;
    }
};

}  // namespace magda::test
